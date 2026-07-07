// 便携 POSIX 沙箱后端：fork + setrlimit + wait4/getrusage。
// 无 seccomp / namespace / cgroup 隔离 → is_secure()==false。
// 作用：macOS 唯一后端 & Linux 的不安全回退。生产模式会被拒绝（fail-closed）。

#include "cppjudge/sandbox_internal.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace cppjudge {
namespace {

using sandbox_detail::RawOutcome;

class BuiltinSandbox final : public SandboxBackend {
public:
    SandboxResult execute(const SandboxRequest& req) override;
    bool          is_secure() const override { return false; }
    const char*   name() const override { return "builtin"; }
};

uint64_t mono_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL;
}

// 子进程：重定向 stdio → chdir → setrlimit → execve。
// 任何 setup/exec 失败都把 errno 写入 err_fd（CLOEXEC，exec 成功即自动关闭），
// 父进程据此把"启动失败"判为 SE（判题系统故障），而非用户 RE（修 D12）。
[[noreturn]] void child_exec(const SandboxRequest& req, int err_fd) {
    auto fail = [&](int code) {
        int e = errno;
        ssize_t rc = write(err_fd, &e, sizeof(e));
        (void)rc;
        _exit(code);
    };

    if (!req.stdin_path.empty()) {
        int fd = open(req.stdin_path.c_str(), O_RDONLY);
        if (fd < 0) fail(126);
        dup2(fd, STDIN_FILENO);
        close(fd);
    } else {
        int fd = open("/dev/null", O_RDONLY);
        if (fd >= 0) { dup2(fd, STDIN_FILENO); close(fd); }
    }
    if (!req.stdout_path.empty()) {
        int fd = open(req.stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) fail(126);
        dup2(fd, STDOUT_FILENO);
        close(fd);
    }
    if (!req.stderr_path.empty()) {
        int fd = open(req.stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) fail(126);
        dup2(fd, STDERR_FILENO);
        close(fd);
    }

    if (!req.work_dir.empty() && chdir(req.work_dir.c_str()) != 0) fail(126);

    const Limits& L = req.limits;
    auto set_rl = [](int res, uint64_t v) {
        struct rlimit rl;
        rl.rlim_cur = v;
        rl.rlim_max = v;
        setrlimit(res, &rl);
    };
    if (L.stack_mb)       set_rl(RLIMIT_STACK, L.stack_mb * 1024ULL * 1024ULL);
    if (L.output_size_mb) set_rl(RLIMIT_FSIZE, L.output_size_mb * 1024ULL * 1024ULL);
#if !defined(__APPLE__)
    // macOS 的 RLIMIT_AS 限制的是虚拟地址空间（含巨大的 dyld 共享缓存），
    // 会误杀正常程序 → macOS 上不设，靠 RSS 计量；Linux 真限制走 cgroup。
    if (L.memory_mb)      set_rl(RLIMIT_AS, L.memory_mb * 1024ULL * 1024ULL);
#endif
    if (L.cpu_time_ms) {  // 秒级硬后备，精确 TLE 靠父进程墙上计时 + CPU 采样
        uint64_t secs = (L.cpu_time_ms + 999) / 1000 + 1;
        set_rl(RLIMIT_CPU, secs);
    }

    std::vector<std::string> argv_store, envp_store;
    std::vector<char*> argv, envp;
    sandbox_detail::build_argv(req, argv_store, argv);
    sandbox_detail::build_envp(req, envp_store, envp);

    execve(req.executable.c_str(), argv.data(), envp.data());
    fail(127);  // execve 返回即失败
    __builtin_unreachable();
}

SandboxResult BuiltinSandbox::execute(const SandboxRequest& req) {
    SandboxResult result;

    int err_pipe[2];
    if (pipe(err_pipe) != 0) {
        result.verdict = Verdict::SE;
        result.error_detail = std::string("pipe failed: ") + strerror(errno);
        return result;
    }
    fcntl(err_pipe[0], F_SETFD, FD_CLOEXEC);
    fcntl(err_pipe[1], F_SETFD, FD_CLOEXEC);

    const uint64_t start = mono_ms();
    pid_t child = fork();
    if (child < 0) {
        close(err_pipe[0]);
        close(err_pipe[1]);
        result.verdict = Verdict::SE;
        result.error_detail = std::string("fork failed: ") + strerror(errno);
        return result;
    }
    if (child == 0) {
        close(err_pipe[0]);
        child_exec(req, err_pipe[1]);  // noreturn
    }

    close(err_pipe[1]);

    uint64_t wall_limit = req.limits.wall_time_ms
                              ? req.limits.wall_time_ms
                              : req.limits.cpu_time_ms * 3;
    if (wall_limit == 0) wall_limit = 10000;

    int status = 0;
    struct rusage ru{};
    bool wall_timeout = false;
    while (true) {
        pid_t w = wait4(child, &status, WNOHANG, &ru);
        if (w == child) break;
        if (w < 0 && errno != EINTR) {
            close(err_pipe[0]);
            result.verdict = Verdict::SE;
            result.error_detail = std::string("wait4 failed: ") + strerror(errno);
            return result;
        }
        if (mono_ms() - start >= wall_limit) {
            kill(child, SIGKILL);
            wall_timeout = true;
            wait4(child, &status, 0, &ru);  // 已 SIGKILL，有界回收
            break;
        }
        struct timespec ts{0, 2 * 1000 * 1000};  // 2ms
        nanosleep(&ts, nullptr);
    }
    const uint64_t wall_ms = mono_ms() - start;

    int exec_errno = 0;
    ssize_t n = read(err_pipe[0], &exec_errno, sizeof(exec_errno));
    close(err_pipe[0]);

    RawOutcome o;
    o.secure_backend = false;
    o.wall_time_ms = wall_ms;
    o.wall_timed_out = wall_timeout;
    o.cpu_time_ms = static_cast<uint64_t>(ru.ru_utime.tv_sec) * 1000ULL +
                    ru.ru_utime.tv_usec / 1000ULL;
#if defined(__APPLE__)
    o.memory_kb = static_cast<uint64_t>(ru.ru_maxrss) / 1024ULL;  // macOS: bytes
#else
    o.memory_kb = static_cast<uint64_t>(ru.ru_maxrss);            // Linux: KB
#endif
    o.output_bytes = sandbox_detail::file_size(req.stdout_path);

    if (WIFEXITED(status)) {
        o.exited = true;
        o.exit_code = WEXITSTATUS(status);
        if (n == static_cast<ssize_t>(sizeof(exec_errno)) &&
            (o.exit_code == 126 || o.exit_code == 127)) {
            result.verdict = Verdict::SE;
            result.error_detail =
                "failed to start program: " + std::string(strerror(exec_errno));
            return result;
        }
    } else if (WIFSIGNALED(status)) {
        o.signaled = true;
        o.signal_num = WTERMSIG(status);
    }

    bool truncated = false;
    result.verdict = sandbox_detail::derive_verdict(o, req.limits, truncated);
    result.exit_code = o.exit_code;
    result.signal_num = o.signal_num;
    result.time_ms = o.cpu_time_ms;
    result.wall_time_ms = o.wall_time_ms;
    result.memory_kb = o.memory_kb;
    result.output_truncated = truncated;
    return result;
}

} // namespace

std::unique_ptr<SandboxBackend> make_builtin_sandbox() {
    return std::make_unique<BuiltinSandbox>();
}

} // namespace cppjudge

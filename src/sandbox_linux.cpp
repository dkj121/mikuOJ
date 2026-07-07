// Linux 安全沙箱后端：namespace + cgroup v2 + seccomp + 权限丢弃。
// 子进程执行顺序（绝对不可乱）：
//   open stdio(宿主路径) → dup2 → setup_rootfs(bind+pivot) → chdir(/box)
//   → setrlimit → [ready→父 attach cgroup→proceed 门控] → drop_privileges
//   → seccomp(compile/run profile) → execve
// 修复：D1(cgroup attach-before-exec 门控) D2(挂载暂存+pivot 前开 stdio, 不继承 fd)
//       D3(降权) D4(SIGSYS→SV) D6(编译独立 profile) D7(CPU 采样 kill) D12(errno 管道→SE)

#include "cppjudge/cgroup_manager.h"
#include "cppjudge/ns_manager.h"
#include "cppjudge/sandbox_internal.h"
#include "cppjudge/seccomp_manager.h"

#include <fcntl.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <sys/resource.h>  // setrlimit / rlimit
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace cppjudge {
namespace {

using sandbox_detail::RawOutcome;

constexpr const char* kBox = "/box";  // work_dir 在沙箱内的挂载点

uint64_t mono_ms() {
    struct timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000ULL + ts.tv_nsec / 1000000ULL;
}

std::string unique_id() {
    static std::atomic<uint64_t> counter{0};
    std::random_device rd;
    uint64_t rnd = (static_cast<uint64_t>(rd()) << 32) ^ rd();
    return std::to_string(getpid()) + "-" +
           std::to_string(counter.fetch_add(1)) + "-" +
           std::to_string(rnd);
}

struct ChildContext {
    const SandboxRequest* req;
    std::vector<ns::MountEntry> mounts;
    std::string new_root;
    SeccompProfile profile;
    int ready_w;     // 子→父：setup 完成
    int proceed_r;   // 父→子：cgroup 已 attach，放行
    int err_w;       // 子→父：errno（CLOEXEC，execve 成功即关）→ SE
};

// 子进程入口。返回值即退出码。
int child_main(ChildContext* ctx) {
    const SandboxRequest& req = *ctx->req;
    auto fail = [&](int code) -> int {
        int e = errno;
        ssize_t rc = write(ctx->err_w, &e, sizeof(e));
        (void)rc;
        _exit(code);
    };

    // 1. 打开 stdio（宿主路径，pivot 前）→ dup2；失败绝不回退继承 fd（D2）
    {
        int fd = req.stdin_path.empty() ? open("/dev/null", O_RDONLY)
                                        : open(req.stdin_path.c_str(), O_RDONLY);
        if (fd < 0) return fail(126);
        dup2(fd, STDIN_FILENO);
        if (fd != STDIN_FILENO) close(fd);
    }
    if (!req.stdout_path.empty()) {
        int fd = open(req.stdout_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return fail(126);
        dup2(fd, STDOUT_FILENO);
        if (fd != STDOUT_FILENO) close(fd);
    }
    if (!req.stderr_path.empty()) {
        int fd = open(req.stderr_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) return fail(126);
        dup2(fd, STDERR_FILENO);
        if (fd != STDERR_FILENO) close(fd);
    }

    // 2. 新根：bind 白名单 + pivot_root（D2 挂载暂存）
    auto setup = ns::Manager::setup_rootfs(ctx->mounts, ctx->new_root);
    if (!setup.ok) {
        dprintf(STDERR_FILENO, "setup_rootfs failed: %s\n", setup.error.c_str());
        return fail(126);  // 判题系统故障 → SE（D12）
    }

    // 3. 进入工作目录
    if (chdir(kBox) != 0) return fail(126);

    // 4. 资源软限制
    // （内存/CPU 主限制走 cgroup；此处设 stack/fsize + CPU 硬后备）
    {
        const Limits& L = req.limits;
        auto set_rl = [](int res, uint64_t v) {
            struct rlimit rl; rl.rlim_cur = v; rl.rlim_max = v; setrlimit(res, &rl);
        };
        if (L.stack_mb)       set_rl(RLIMIT_STACK, L.stack_mb * 1024ULL * 1024ULL);
        if (L.output_size_mb) set_rl(RLIMIT_FSIZE, L.output_size_mb * 1024ULL * 1024ULL);
        if (L.cpu_time_ms)    set_rl(RLIMIT_CPU, (L.cpu_time_ms + 999) / 1000 + 1);
    }

    // 5. 通知父就绪，等待父 attach cgroup 后放行（D1 attach-before-exec 门控）
    {
        char ready = 1;
        ssize_t rc = write(ctx->ready_w, &ready, 1);
        (void)rc;
        close(ctx->ready_w);
        char go = 0;
        while (read(ctx->proceed_r, &go, 1) < 0 && errno == EINTR) {
        }
        close(ctx->proceed_r);
    }

    // 6. 丢弃权限（D3）
    if (!ns::Manager::drop_privileges()) return fail(126);

    // 7. seccomp —— execve 前绝对最后一步。
    //   运行阶段：严格白名单（默认拒绝，违规 SIGSYS→SV）。
    //   编译阶段：不装 seccomp —— 编译器(gcc/go/rustc/javac)可信但 syscall 面极广，
    //     其隔离由 net namespace(无网络) + cgroup + 降权(nobody) + 最小文件系统 保证（D6）。
    if (!req.is_compile) {
        if (!seccomp::Manager::install(ctx->profile, false)) return fail(126);
    }

    // 8. execve
    std::vector<std::string> argv_store, envp_store;
    std::vector<char*> argv, envp;
    sandbox_detail::build_argv(req, argv_store, argv);
    sandbox_detail::build_envp(req, envp_store, envp);
    execve(req.executable.c_str(), argv.data(), envp.data());
    dprintf(STDERR_FILENO,
            "execve failed: executable=%s errno=%d (%s) access=%d ld_access=%d\n",
            req.executable.c_str(),
            errno,
            strerror(errno),
            access(req.executable.c_str(), X_OK),
            access("/lib64/ld-linux-x86-64.so.2", X_OK));
    return fail(127);
}

class LinuxNsSandbox final : public SandboxBackend {
public:
    SandboxResult execute(const SandboxRequest& req) override;
    const char*   name() const override { return "linux-ns"; }
};

SandboxResult LinuxNsSandbox::execute(const SandboxRequest& req) {
    SandboxResult result;

    // 忽略 SIGPIPE：子进程 setup 期间早退会关闭 proceed 管道读端，父进程随后写入会
    // 触发 SIGPIPE，其默认行为会杀死整个判题进程。忽略后 write 返回 EPIPE，可正常处理。
    static const bool sigpipe_ignored = [] {
        struct sigaction sa{};
        sa.sa_handler = SIG_IGN;
        sigaction(SIGPIPE, &sa, nullptr);
        return true;
    }();
    (void)sigpipe_ignored;

    // 1. cgroup
    const std::string id = unique_id();
    auto cg = cgroup::Manager::create(id);
    if (!cg.is_valid()) {
        result.verdict = Verdict::SE;
        result.error_detail = "failed to create cgroup (delegation?): " + id;
        return result;
    }
    cgroup::Limits cl;
    cl.cpu_time_us = req.limits.cpu_time_ms * 1000ULL;
    cl.memory_bytes = req.limits.memory_mb * 1024ULL * 1024ULL;
    cl.max_pids = req.limits.max_processes ? req.limits.max_processes : 64;
    if (!cg.apply(cl)) {
        result.verdict = Verdict::SE;
        result.error_detail = "failed to apply cgroup limits (controllers not delegated)";
        cg.destroy();
        return result;
    }

    // 2. 挂载条目：work_dir→/box(rw) + 语言依赖 + 基础库
    std::vector<ns::MountEntry> mounts;
    mounts.push_back({req.work_dir, kBox, true});
    for (const auto& m : req.extra_mounts) mounts.push_back(m);
    // 基础只读依赖。含 /usr/share：Debian/Ubuntu 下 GOROOT/src、部分 rustlib、
    // JVM 资源以符号链接指向 /usr/share，缺它会导致 Go/Rust/Java 编译失败。
    for (const char* base : {"/bin", "/lib", "/lib64", "/usr/lib", "/usr/lib64",
                             "/usr/libexec", "/usr/bin", "/usr/share",
                             "/etc/alternatives"}) {
        mounts.push_back({base, base, false});
    }

    std::string new_root = "/tmp/cppjudge." + id;
    mkdir(new_root.c_str(), 0755);

    // 3. 同步管道
    int ready_pipe[2], proceed_pipe[2], err_pipe[2];
    if (pipe2(ready_pipe, O_CLOEXEC) != 0 || pipe2(proceed_pipe, 0) != 0 ||
        pipe2(err_pipe, O_CLOEXEC) != 0) {
        result.verdict = Verdict::SE;
        result.error_detail = "pipe2 failed";
        cg.destroy();
        return result;
    }

    ChildContext ctx;
    ctx.req = &req;
    ctx.mounts = mounts;
    ctx.new_root = new_root;
    ctx.profile = seccomp::profile_for_lang(req.lang);
    ctx.ready_w = ready_pipe[1];
    ctx.proceed_r = proceed_pipe[0];
    ctx.err_w = err_pipe[1];

    const uint64_t start = mono_ms();
    pid_t child = ns::Manager::clone_and_exec(
        ns::Manager::ALL_NS_FLAGS, [&ctx] { return child_main(&ctx); });

    // 父进程关闭子端
    close(ready_pipe[1]);
    close(proceed_pipe[0]);
    close(err_pipe[1]);

    if (child < 0) {
        result.verdict = Verdict::SE;
        result.error_detail = std::string("clone failed: ") + strerror(errno);
        close(ready_pipe[0]); close(proceed_pipe[1]); close(err_pipe[0]);
        cg.destroy();
        rmdir(new_root.c_str());
        return result;
    }

    // 4. attach 到 cgroup（在放行前，保证 execve 前限制生效；修 D1）
    // attach 失败意味着用户进程不在任何 cgroup 内：内存/pids 限制失效，且超时 kill
    // 依赖的 cgroup.kill 无法终止它 → 后续 waitpid 会永久阻塞。必须视为 SE 并杀子进程。
    if (!cg.attach(child)) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        close(ready_pipe[0]); close(proceed_pipe[1]); close(err_pipe[0]);
        cg.destroy();
        rmdir(new_root.c_str());
        result.verdict = Verdict::SE;
        result.error_detail = "failed to attach child to cgroup";
        return result;
    }

    // 5. 等子就绪（有界），然后放行
    {
        struct pollfd pfd{ready_pipe[0], POLLIN, 0};
        if (poll(&pfd, 1, 5000) <= 0) {
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
            close(ready_pipe[0]); close(proceed_pipe[1]); close(err_pipe[0]);
            cg.destroy();
            rmdir(new_root.c_str());
            result.verdict = Verdict::SE;
            result.error_detail = "child setup timed out";
            return result;
        }
        // 区分“子进程就绪”与“子进程 setup 中途死亡”：read 返回 0(EOF) 表示子进程已关闭
        // ready 写端而退出，此时不能放行，应判 SE（否则 proceed 写入触发 EPIPE）。
        char b; ssize_t rc = read(ready_pipe[0], &b, 1);
        close(ready_pipe[0]);
        if (rc <= 0) {
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
            close(proceed_pipe[1]); close(err_pipe[0]);
            cg.destroy();
            rmdir(new_root.c_str());
            result.verdict = Verdict::SE;
            result.error_detail = "child died during setup";
            return result;
        }
        char go = 1; rc = write(proceed_pipe[1], &go, 1); (void)rc;
        close(proceed_pipe[1]);
    }

    // 6. 墙上超时 + CPU 采样 kill（D7）
    const uint64_t wall_limit = req.limits.wall_time_ms
                                    ? req.limits.wall_time_ms
                                    : req.limits.cpu_time_ms * 3;
    const uint64_t out_cap = req.limits.output_size_mb
                                 ? req.limits.output_size_mb * 1024ULL * 1024ULL : 0;
    int status = 0;
    bool wall_timeout = false, cpu_timeout = false, output_over = false;
    bool killed = false;               // 是否走了主动 kill 路径（stats 需在 destroy 前抓取）
    cgroup::Stats kill_stats;
    while (true) {
        pid_t w = waitpid(child, &status, WNOHANG);
        if (w == child) break;
        if (w < 0 && errno != EINTR) {
            // waitpid 硬错误：杀子进程并直接返回 SE（不能落到 derive_verdict，否则
            // 未 signaled/exited 的 outcome 会被判成 AC）。
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
            std::string err = std::string("waitpid: ") + strerror(errno);
            close(err_pipe[0]);
            cg.destroy();
            rmdir(new_root.c_str());
            result.verdict = Verdict::SE;
            result.error_detail = err;
            return result;
        }
        uint64_t elapsed = mono_ms() - start;
        cgroup::Stats live = cg.collect();
        if (req.limits.cpu_time_ms && live.cpu_usage_us / 1000ULL > req.limits.cpu_time_ms) {
            cpu_timeout = true;
        }
        // 输出超限主动 kill（SIGXFSZ 未必可靠终止 busy-loop）
        if (out_cap && sandbox_detail::file_size(req.stdout_path) >= out_cap) {
            output_over = true;
        }
        if (output_over || (wall_limit && elapsed >= wall_limit) || cpu_timeout) {
            wall_timeout = wall_timeout || (wall_limit && elapsed >= wall_limit);
            kill_stats = cg.collect();  // 必须在 destroy 前抓取，否则统计归零
            killed = true;
            cg.destroy();               // cgroup.kill 整个子树
            waitpid(child, &status, 0); // 有界回收
            break;
        }
        struct timespec ts{0, 3 * 1000 * 1000};  // 3ms
        nanosleep(&ts, nullptr);
    }
    const uint64_t wall_ms = mono_ms() - start;

    // 7. 读 setup/exec 错误码
    int child_errno = 0;
    ssize_t n = read(err_pipe[0], &child_errno, sizeof(child_errno));
    close(err_pipe[0]);

    // 8. 资源统计：kill 路径已在 destroy 前采集，否则此处 collect
    cgroup::Stats stats = killed ? kill_stats : cg.collect();

    RawOutcome o;
    o.secure_backend = true;
    o.wall_time_ms = wall_ms;
    o.wall_timed_out = wall_timeout;
    o.cpu_timed_out = cpu_timeout;
    o.output_exceeded = output_over;
    o.cpu_time_ms = stats.cpu_usage_us / 1000ULL;
    o.memory_kb = stats.memory_peak_kb ? stats.memory_peak_kb : stats.memory_kb;
    o.oom_killed = stats.oom_killed;
    o.output_bytes = sandbox_detail::file_size(req.stdout_path);

    if (WIFEXITED(status)) {
        o.exited = true;
        o.exit_code = WEXITSTATUS(status);
        if (n == static_cast<ssize_t>(sizeof(child_errno)) &&
            (o.exit_code == 126 || o.exit_code == 127)) {
            result.verdict = Verdict::SE;
            result.error_detail =
                "sandbox setup/exec failed: " + std::string(strerror(child_errno));
            cg.destroy();
            rmdir(new_root.c_str());
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

    cg.destroy();
    rmdir(new_root.c_str());
    return result;
}

} // namespace

std::unique_ptr<SandboxBackend> make_linux_ns_sandbox() {
    return std::make_unique<LinuxNsSandbox>();
}

} // namespace cppjudge

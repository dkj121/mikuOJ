#include "cppjudge/sandbox_internal.h"

#include <sys/stat.h>

#include <csignal>

namespace cppjudge::sandbox_detail {

Verdict derive_verdict(const RawOutcome& o, const Limits& limits,
                       bool& output_truncated) {
    output_truncated = false;

    const uint64_t out_cap =
        limits.output_size_mb ? limits.output_size_mb * 1024ULL * 1024ULL : 0;
    if (out_cap && o.output_bytes >= out_cap) {
        output_truncated = true;
    }

    // 1. 时间超限（墙上或 CPU）——最高优先级
    const bool cpu_over =
        o.cpu_timed_out ||
        (limits.cpu_time_ms && o.cpu_time_ms > limits.cpu_time_ms);
    if (o.wall_timed_out || cpu_over) {
        return Verdict::TLE;
    }

    // 2. 内存超限（OOM 或峰值 RSS 超限）
    const bool mem_over =
        o.oom_killed ||
        (limits.memory_mb && o.memory_kb > limits.memory_mb * 1024ULL);
    if (mem_over) {
        return Verdict::MLE;
    }

    // 3. 信号终止
    if (o.signaled) {
        switch (o.signal_num) {
            case SIGSYS:                            // seccomp 拦截
                return o.secure_backend ? Verdict::SV : Verdict::RE;
            case SIGXCPU:                           // RLIMIT_CPU
                return Verdict::TLE;
            case SIGXFSZ:                           // RLIMIT_FSIZE
                output_truncated = true;
                return Verdict::OLE;
            default:
                return Verdict::RE;
        }
    }

    // 4. 正常退出但输出被截断 → OLE
    if (output_truncated) {
        return Verdict::OLE;
    }

    // 5. 非零退出码 → RE
    if (o.exited && o.exit_code != 0) {
        return Verdict::RE;
    }

    // 6. 干净退出 → AC（是否 WA 由 Comparator 决定）
    return Verdict::AC;
}

std::vector<std::string> minimal_env(const std::string& home_dir) {
    std::vector<std::string> env;
    env.push_back("PATH=/usr/local/bin:/usr/bin:/bin:/opt/homebrew/bin");
    env.push_back("HOME=" + (home_dir.empty() ? std::string("/tmp") : home_dir));
    env.push_back("LANG=C");
    env.push_back("LC_ALL=C");
    return env;
}

void build_argv(const SandboxRequest& req,
                std::vector<std::string>& storage,
                std::vector<char*>& out_ptrs) {
    storage.clear();
    storage.reserve(req.argv.size() + 1);
    storage.push_back(req.executable);
    for (const auto& a : req.argv) storage.push_back(a);

    out_ptrs.clear();
    out_ptrs.reserve(storage.size() + 1);
    for (auto& s : storage) out_ptrs.push_back(s.data());
    out_ptrs.push_back(nullptr);
}

void build_envp(const SandboxRequest& req,
                std::vector<std::string>& storage,
                std::vector<char*>& out_ptrs) {
    storage = minimal_env(req.work_dir);
    for (const auto& e : req.envp) storage.push_back(e);

    out_ptrs.clear();
    out_ptrs.reserve(storage.size() + 1);
    for (auto& s : storage) out_ptrs.push_back(s.data());
    out_ptrs.push_back(nullptr);
}

uint64_t file_size(const std::string& path) {
    struct stat st{};
    if (path.empty() || stat(path.c_str(), &st) != 0) return 0;
    return static_cast<uint64_t>(st.st_size);
}

} // namespace cppjudge::sandbox_detail

namespace cppjudge {

std::unique_ptr<SandboxBackend> make_sandbox(const std::string& type,
                                             std::string& error) {
#if defined(__linux__)
    if (type == "auto" || type == "linux-ns" || type == "nsjail") {
        return make_linux_ns_sandbox();
    }
    if (type == "builtin") {
        return make_builtin_sandbox();
    }
#elif defined(__APPLE__)
    if (type == "auto" || type == "builtin") {
        return make_builtin_sandbox();
    }
    if (type == "linux-ns" || type == "nsjail") {
        error = "secure Linux sandbox is unavailable on macOS (development build); "
                "use --sandbox-type builtin";
        return nullptr;
    }
#endif
    error = "unknown sandbox type: '" + type + "'";
    return nullptr;
}

} // namespace cppjudge

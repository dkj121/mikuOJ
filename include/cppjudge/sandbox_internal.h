#pragma once

// 沙箱后端的共享编排内部接口（不对外暴露）。
// 两个平台后端（sandbox_linux.cpp / sandbox_darwin.cpp）复用这里的判决推导、
// 环境构造与 argv 拼装，避免重复实现。

#include "cppjudge/common.h"
#include "cppjudge/sandbox.h"

#include <cstdint>
#include <string>
#include <vector>

namespace cppjudge::sandbox_detail {

// 子进程结束后的原始观测量（平台后端负责填充）。
struct RawOutcome {
    bool     exited         = false;
    int      exit_code      = 0;
    bool     signaled       = false;
    int      signal_num     = 0;
    bool     wall_timed_out = false;   // 父进程因墙上超时主动 kill
    bool     cpu_timed_out  = false;   // 后端判定 CPU 超时
    bool     oom_killed     = false;   // cgroup / 观测判定 OOM
    uint64_t cpu_time_ms    = 0;       // 用户态 CPU 时间
    uint64_t wall_time_ms   = 0;
    uint64_t memory_kb      = 0;       // 峰值 RSS
    uint64_t output_bytes   = 0;       // stdout 实际字节（0 = 未捕获）
    bool     secure_backend = true;    // SIGSYS→SV 仅安全后端成立
};

// 由原始观测量 + 资源限制推导判决，并给出 output_truncated。
// 判决优先级：TLE > MLE > SV > OLE > RE > AC（AC 之后交给 Comparator 判 WA）。
Verdict derive_verdict(const RawOutcome& o, const Limits& limits,
                       bool& output_truncated);

// 最小安全环境（PATH/HOME/LANG），绝不继承 host environ（修 D5）。
std::vector<std::string> minimal_env(const std::string& home_dir);

// argv = [executable, req.argv..., nullptr]；storage 持有字符串所有权。
void build_argv(const SandboxRequest& req,
                std::vector<std::string>& storage,
                std::vector<char*>& out_ptrs);

// envp = minimal_env(work_dir) + req.envp + nullptr；storage 持有所有权。
void build_envp(const SandboxRequest& req,
                std::vector<std::string>& storage,
                std::vector<char*>& out_ptrs);

// 文件字节数（不存在返回 0）。
uint64_t file_size(const std::string& path);

} // namespace cppjudge::sandbox_detail

namespace cppjudge {

// 平台后端工厂（由各平台 .cpp 定义，make_sandbox 分发）。
std::unique_ptr<SandboxBackend> make_builtin_sandbox();      // 便携 POSIX，两平台都有
#if defined(__linux__)
std::unique_ptr<SandboxBackend> make_linux_ns_sandbox();     // 仅 Linux 安全后端
#endif

} // namespace cppjudge

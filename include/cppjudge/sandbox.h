#pragma once

#include "cppjudge/common.h"

#include <memory>
#include <string>
#include <vector>

namespace cppjudge {

// ============================================================
// 沙箱执行请求（编译阶段与运行阶段共用）
// ============================================================
struct SandboxRequest {
    Limits      limits;
    std::string executable;                    // 要执行的可执行文件（解释器或编译产物，绝对路径）
    std::vector<std::string> argv;             // executable 之后的参数（不含 argv[0]）
    std::vector<std::string> envp;             // 追加到最小安全环境(PATH/HOME/LANG)之上的额外变量(如 GOCACHE)；绝不继承 host environ
    std::string stdin_path;                    // 宿主机路径；为空 → /dev/null
    std::string stdout_path;                   // 宿主机路径；为空 → 丢弃
    std::string stderr_path;                   // 宿主机路径；为空 → 丢弃
    std::string work_dir;                      // 沙箱工作目录（宿主机路径，需可写）
    std::string lang;                          // "cpp"/"python3"/... 决定 seccomp profile
    bool        is_compile = false;            // 编译阶段：放宽 syscall 策略（见 D6）
    std::vector<ns::MountEntry> extra_mounts;  // 语言运行时依赖（仅 Linux 后端使用）
};

// ============================================================
// 单次沙箱执行结果
// ============================================================
struct SandboxResult {
    Verdict     verdict          = Verdict::AC;
    int         exit_code        = 0;
    int         signal_num       = 0;
    uint64_t    time_ms          = 0;   // 用户态 CPU 时间
    uint64_t    wall_time_ms     = 0;   // 墙上时间
    uint64_t    memory_kb        = 0;   // 峰值 RSS
    bool        output_truncated = false;
    std::string error_detail;           // SE 时判题系统内部错误细节
};

// ============================================================
// 沙箱后端抽象。
//   - LinuxNsSandbox（sandbox_linux.cpp，__linux__）：ns + cgroup + seccomp + privdrop，安全。
//   - BuiltinSandbox（sandbox_darwin.cpp / Linux 回退）：fork + setrlimit + getrusage，不安全。
// ============================================================
class SandboxBackend {
public:
    virtual ~SandboxBackend() = default;

    virtual SandboxResult execute(const SandboxRequest& req) = 0;

    // 是否提供真正的隔离；不安全后端在生产模式被拒绝（fail-closed）。
    virtual bool          is_secure() const = 0;
    virtual const char*   name()      const = 0;
};

// 后端类型标识（问题配置里的 sandbox_type 会映射到这些别名）。
//   "auto"                 → 安全后端可用则用之，否则 builtin
//   "builtin"              → 不安全的 rlimit 后端
//   "linux-ns" / "nsjail"  → Linux 安全后端（仅 __linux__）
// 未知或当前平台不可用时返回 nullptr 并写入 error。
std::unique_ptr<SandboxBackend> make_sandbox(const std::string& type, std::string& error);

} // namespace cppjudge

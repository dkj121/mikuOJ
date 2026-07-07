#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cppjudge {

// ============================================================
// 判决枚举（按优先级从高到低排列）
// ============================================================
enum class Verdict {
    AC,   // Accepted — 全部测试点通过
    WA,   // Wrong Answer — 正常退出但输出不符
    TLE,  // Time Limit Exceeded — CPU 或墙上时间超限
    MLE,  // Memory Limit Exceeded — 内存超限
    OLE,  // Output Limit Exceeded — 输出大小超限
    RE,   // Runtime Error — 非零退出或信号终止
    SV,   // Syscall Violation — seccomp 拦截
    CE,   // Compile Error — 编译失败
    SE,   // System Error — 判题系统自身故障
};

inline const char* verdict_to_string(Verdict v) {
    switch (v) {
        case Verdict::AC:  return "Accepted";
        case Verdict::WA:  return "Wrong Answer";
        case Verdict::TLE: return "Time Limit Exceeded";
        case Verdict::MLE: return "Memory Limit Exceeded";
        case Verdict::OLE: return "Output Limit Exceeded";
        case Verdict::RE:  return "Runtime Error";
        case Verdict::SV:  return "Syscall Violation";
        case Verdict::CE:  return "Compile Error";
        case Verdict::SE:  return "System Error";
    }
    return "UNKNOWN";
}

// ============================================================
// 资源限制（来自 problem.json 或 CLI 覆盖）
// ============================================================
struct Limits {
    uint64_t cpu_time_ms          = 2000;
    uint64_t wall_time_ms         = 6000;   // 默认 CPU × 3
    uint64_t memory_mb            = 256;
    uint64_t stack_mb             = 8;
    uint64_t output_size_mb       = 10;
    uint32_t max_processes        = 64;   // 允许多线程运行时(Go/JVM/线程 Python)，仍防 fork 炸弹(修 D9)
    uint64_t compile_time_ms      = 5000;
};

// ============================================================
// 单个测试点的执行结果
// ============================================================
struct RunResult {
    Verdict    verdict        = Verdict::AC;
    int        exit_code      = 0;
    int        signal_num     = 0;
    uint64_t   time_ms        = 0;    // 用户态 CPU 时间
    uint64_t   wall_time_ms   = 0;
    uint64_t   memory_kb      = 0;    // 峰值 RSS
    bool       output_truncated = false;
    int        test_index     = 0;
    std::string run_id;
    std::string run_dir;
    std::string compare_detail;       // WA 时的差异描述
    std::string error_detail;         // SE/CE 时的详细信息
};

// ============================================================
// 判题配置（合并 problem.json + CLI 覆盖）
// ============================================================
struct JudgeConfig {
    std::string problem_dir;
    std::string submission_file;
    Limits      limits;
    std::string compare_mode;    // "exact" | "floating"
    std::string sandbox_type;    // "auto" | "linux-ns" | "nsjail"
    double      float_abs_eps = 1e-9;
    double      float_rel_eps = 1e-6;
    bool        verbose       = false;
};

// ============================================================
// Namespace 挂载条目（ns 和 compiler 共用）
// ============================================================
namespace ns {

struct MountEntry {
    std::string source;    // 宿主机路径
    std::string target;    // 沙箱内路径
    bool        writable = false;
};

} // namespace ns

// ============================================================
// 语言枚举和配置（由 Language Manager 使用）
// ============================================================
enum class Language {
    CPP, C, PYTHON3, JAVA, GO, RUST,
    UNKNOWN
};

inline const char* language_to_string(Language lang) {
    switch (lang) {
        case Language::CPP:      return "cpp";
        case Language::C:        return "c";
        case Language::PYTHON3:  return "python3";
        case Language::JAVA:     return "java";
        case Language::GO:       return "go";
        case Language::RUST:     return "rust";
        default:                 return "unknown";
    }
}

inline Language language_from_string(const std::string& s) {
    if (s == "cpp" || s == "c++")        return Language::CPP;
    if (s == "c")                        return Language::C;
    if (s == "python3" || s == "python" || s == "py") return Language::PYTHON3;
    if (s == "java")                     return Language::JAVA;
    if (s == "go")                       return Language::GO;
    if (s == "rust" || s == "rs")        return Language::RUST;
    return Language::UNKNOWN;
}

struct LanguageConfig {
    Language lang;
    std::string name;                     // "cpp", "python3", ...
    std::vector<std::string> extensions;  // {".cpp", ".cc", ".cxx"}
    std::string compiler_path;            // "/usr/bin/g++" 或 "" (解释型)
    std::vector<std::string> compile_args;// {"-std=c++20", "-O2", ...}
    std::string runtime_path;             // "./solution" 或 "/usr/bin/python3"
    std::vector<std::string> runtime_args;// 解释型语言的脚本参数
    bool needs_compilation;               // false → 直接解释执行
};

} // namespace cppjudge

#pragma once

#include "cppjudge/common.h"

#include <memory>
#include <string>
#include <vector>

namespace cppjudge {

struct Problem {
    std::string title;
    std::string problem_dir;
    Limits      limits;
    std::string compare_mode = "exact";   // "exact" | "floating"
    std::string sandbox_type = "auto";    // "auto" | "builtin" | "linux-ns"
    double      float_abs_eps = 1e-9;
    double      float_rel_eps = 1e-6;

    struct TestCase {
        std::string input_file;   // 绝对/相对路径
        std::string output_file;
        int         index = 0;
    };
    std::vector<TestCase> test_cases;
};

class ProblemManager {
public:
    // ---- 限制上界 ----
    static constexpr uint64_t kMaxCpuTimeMs      = 60'000;    // 60 s
    static constexpr uint64_t kMaxWallTimeMs     = 180'000;   // 3 min
    static constexpr uint64_t kMaxMemoryMb       = 16 * 1024; // 16 GiB
    static constexpr uint64_t kMaxOutputMb       = 1024;      // 1 GiB
    static constexpr uint64_t kMaxCompileTimeMs  = 120'000;   // 2 min

    // 无溢出的墙上时间计算（clamp to kMaxWallTimeMs）。
    static uint64_t safe_wall_time(uint64_t cpu_time_ms);

    // 从目录加载题目（problem.json + input/*.in + output/*.out）。失败返回 nullptr 并写 error。
    [[nodiscard]] static std::unique_ptr<Problem> load(
        const std::string& problem_dir, std::string& error);

    // 校验题目完整性。
    [[nodiscard]] static bool validate(const Problem& problem, std::string& error);
};

} // namespace cppjudge

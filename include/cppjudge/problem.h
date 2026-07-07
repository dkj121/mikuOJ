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
    std::string sandbox_type = "auto";    // "auto" | "linux-ns" | "nsjail"
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
    // 从目录加载题目（problem.json + input/*.in + output/*.out）。失败返回 nullptr 并写 error。
    static std::unique_ptr<Problem> load(const std::string& problem_dir,
                                         std::string& error);

    // 校验题目完整性。
    static bool validate(const Problem& problem, std::string& error);
};

} // namespace cppjudge

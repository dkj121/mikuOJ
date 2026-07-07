#pragma once

#include <string>

namespace cppjudge {

struct CompareResult {
    bool        is_match       = false;
    std::string detail;
    int         mismatch_line  = 0;   // exact 模式：不同的行号（1-based，0=无）
    int         mismatch_token = 0;   // floating 模式：不同的 token 序号（1-based）
    std::string user_value;
    std::string expected_value;
};

// 输出比较引擎：exact（逐行，空白规范化）与 floating（逐 token，绝对/相对误差）。
class Comparator {
public:
    static CompareResult compare_exact(
        const std::string& user_output,
        const std::string& expected_output,
        bool trim_trailing_spaces   = true,
        bool trim_trailing_newlines = true,
        bool ignore_empty_lines     = false);

    static CompareResult compare_floating(
        const std::string& user_output,
        const std::string& expected_output,
        double abs_eps = 1e-9,
        double rel_eps = 1e-6);
};

} // namespace cppjudge

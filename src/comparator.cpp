#include "cppjudge/comparator.h"

#include <algorithm>
#include <cmath>
#include <locale>
#include <sstream>
#include <vector>

namespace cppjudge {

namespace {

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> lines;
    std::istringstream stream(s);
    std::string line;
    while (std::getline(stream, line)) lines.push_back(line);
    return lines;
}

std::vector<std::string> tokenize(const std::string& s) {
    std::vector<std::string> tokens;
    std::istringstream stream(s);
    std::string token;
    while (stream >> token) tokens.push_back(token);
    return tokens;
}

// locale 无关的浮点解析（修 D17：strtod 依赖 LC_NUMERIC）。
// 支持 nan / inf / -inf；要求整个 token 被消费。
bool parse_double(const std::string& s, double& out) {
    if (s == "nan" || s == "NaN")  { out = std::nan(""); return true; }
    if (s == "inf" || s == "+inf" || s == "Infinity")  { out = HUGE_VAL;  return true; }
    if (s == "-inf" || s == "-Infinity") { out = -HUGE_VAL; return true; }

    std::istringstream iss(s);
    iss.imbue(std::locale::classic());
    double v = 0.0;
    iss >> v;
    if (iss.fail()) return false;
    char extra;
    if (iss >> extra) return false;  // 尾部有非空白 → 不是纯数字 token
    out = v;
    return true;
}

} // namespace

CompareResult Comparator::compare_exact(
    const std::string& user_output,
    const std::string& expected_output,
    bool trim_trailing_spaces,
    bool trim_trailing_newlines,
    bool ignore_empty_lines) {
    CompareResult result;
    result.is_match = true;

    auto user_lines = split_lines(user_output);
    auto expected_lines = split_lines(expected_output);

    if (trim_trailing_spaces) {
        auto trim_right = [](std::string& s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t' ||
                                  s.back() == '\r')) {
                s.pop_back();
            }
        };
        for (auto& l : user_lines) trim_right(l);
        for (auto& l : expected_lines) trim_right(l);
    }

    if (trim_trailing_newlines) {
        while (!user_lines.empty() && user_lines.back().empty())
            user_lines.pop_back();
        while (!expected_lines.empty() && expected_lines.back().empty())
            expected_lines.pop_back();
    }

    if (ignore_empty_lines) {
        auto remove_empty = [](std::vector<std::string>& v) {
            v.erase(std::remove_if(v.begin(), v.end(),
                                   [](const std::string& s) { return s.empty(); }),
                    v.end());
        };
        remove_empty(user_lines);
        remove_empty(expected_lines);
    }

    size_t max_lines = std::max(user_lines.size(), expected_lines.size());
    for (size_t i = 0; i < max_lines; ++i) {
        if (i >= user_lines.size()) {
            result.is_match = false;
            result.mismatch_line = static_cast<int>(i + 1);
            result.detail = "user output has fewer lines than expected";
            result.user_value = "(end of file)";
            result.expected_value = expected_lines[i];
            return result;
        }
        if (i >= expected_lines.size()) {
            result.is_match = false;
            result.mismatch_line = static_cast<int>(i + 1);
            result.detail = "user output has more lines than expected";
            result.user_value = user_lines[i];
            result.expected_value = "(end of file)";
            return result;
        }
        if (user_lines[i] != expected_lines[i]) {
            result.is_match = false;
            result.mismatch_line = static_cast<int>(i + 1);
            result.detail = "line " + std::to_string(i + 1) + " differs";
            result.user_value = user_lines[i];
            result.expected_value = expected_lines[i];
            return result;
        }
    }

    return result;
}

CompareResult Comparator::compare_floating(
    const std::string& user_output,
    const std::string& expected_output,
    double abs_eps,
    double rel_eps) {
    CompareResult result;
    result.is_match = true;

    auto user_tokens = tokenize(user_output);
    auto expected_tokens = tokenize(expected_output);

    if (user_tokens.size() != expected_tokens.size()) {
        result.is_match = false;
        result.mismatch_token = static_cast<int>(
            std::min(user_tokens.size(), expected_tokens.size())) + 1;
        result.detail = "token count differs: user=" +
                        std::to_string(user_tokens.size()) +
                        " expected=" + std::to_string(expected_tokens.size());
        return result;
    }

    for (size_t i = 0; i < user_tokens.size(); ++i) {
        const std::string& u = user_tokens[i];
        const std::string& e = expected_tokens[i];

        double uv = 0.0, ev = 0.0;
        bool u_num = parse_double(u, uv);
        bool e_num = parse_double(e, ev);

        if (u_num && e_num) {
            if (std::isnan(uv) && std::isnan(ev)) continue;
            if (std::isinf(uv) || std::isinf(ev)) {
                if (uv == ev) continue;
                result.is_match = false;
                result.mismatch_token = static_cast<int>(i + 1);
                result.user_value = u;
                result.expected_value = e;
                result.detail = "infinity mismatch at token " + std::to_string(i + 1);
                return result;
            }
            if (uv == 0.0 && ev == 0.0) continue;  // 0.0 == -0.0

            double diff = std::abs(uv - ev);
            if (diff <= abs_eps) continue;               // 绝对误差（边界含）
            double denom = std::max(1.0, std::abs(ev));
            if (diff / denom <= rel_eps) continue;       // 相对误差

            result.is_match = false;
            result.mismatch_token = static_cast<int>(i + 1);
            result.user_value = u;
            result.expected_value = e;
            result.detail = "float mismatch at token " + std::to_string(i + 1);
            return result;
        }

        if (!u_num && !e_num) {
            if (u != e) {
                result.is_match = false;
                result.mismatch_token = static_cast<int>(i + 1);
                result.user_value = u;
                result.expected_value = e;
                result.detail = "string mismatch at token " + std::to_string(i + 1);
                return result;
            }
            continue;
        }

        result.is_match = false;
        result.mismatch_token = static_cast<int>(i + 1);
        result.user_value = u;
        result.expected_value = e;
        result.detail = std::string("type mismatch at token ") +
                        std::to_string(i + 1) + ": " +
                        (u_num ? "number" : "string") + " vs " +
                        (e_num ? "number" : "string");
        return result;
    }

    return result;
}

} // namespace cppjudge

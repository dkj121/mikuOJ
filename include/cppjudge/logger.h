#pragma once

#include "cppjudge/common.h"

#include <string>
#include <vector>

namespace cppjudge {

class Logger {
public:
    // 创建 per-run 产物目录 <base_dir>/runs/<run_id>/，返回其路径。
    static std::string create_run_dir(const std::string& base_dir);

    // 将判题结果写为 JSON（字段全部转义，见 D13）。
    static bool write_log(const std::string& output_path,
                          const std::string& problem_dir,
                          const std::string& submission_file,
                          Verdict final_verdict,
                          const std::vector<RunResult>& results);

    // JSON 字符串转义（公开以便 CLI 直接构造安全 JSON）。
    static std::string json_escape(const std::string& s);
};

} // namespace cppjudge

#pragma once

// 诊断日志门面：基于 spdlog，统一输出到 stderr（stdout 保留给 JSON 结果）。
// 异常/内部错误的调用栈追踪基于 cpptrace。
// 注意区分：本模块是"运行诊断日志"；判题结果的结构化日志 judge_log.json 由 Logger 负责。

#include <spdlog/spdlog.h>

#include <string>

namespace cppjudge::log {

// 初始化全局 logger（stderr sink、带时间/级别）。幂等，可多次调用。
// verbose=true → debug 级；否则 info 级。
void init(bool verbose = false);

// 安装 cpptrace 终止处理器：未捕获异常时打印带栈的诊断（建议在 main 最早调用）。
void install_crash_handler();

// 记录一条错误并附当前调用栈（cpptrace），用于 SE / 捕获到的内部异常。
void error_with_trace(const std::string& msg);

} // namespace cppjudge::log

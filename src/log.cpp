#include "cppjudge/log.h"

#include <cpptrace/cpptrace.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>

#include <mutex>

namespace cppjudge::log {

void init(bool verbose) {
    static std::once_flag once;
    std::call_once(once, [] {
        auto logger = spdlog::stderr_color_mt("cppjudge");
        logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
        spdlog::set_default_logger(logger);
        spdlog::flush_on(spdlog::level::warn);
    });
    spdlog::set_level(verbose ? spdlog::level::debug : spdlog::level::info);
}

void install_crash_handler() {
    // 未捕获异常 / std::terminate 时打印带解析的调用栈。
    cpptrace::register_terminate_handler();
}

void error_with_trace(const std::string& msg) {
    spdlog::error("{}", msg);
    // skip=1：略过本函数自身
    spdlog::error("stacktrace:\n{}", cpptrace::generate_trace(1).to_string());
}

} // namespace cppjudge::log

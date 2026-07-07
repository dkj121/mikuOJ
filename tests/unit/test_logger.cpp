#include <gtest/gtest.h>

#include <sys/stat.h>

#include <fstream>
#include <iterator>
#include <string>

#include "cppjudge/logger.h"

using namespace cppjudge;

TEST(Logger, JsonEscapeQuotesAndBackslash) {
    EXPECT_EQ(Logger::json_escape("a\"b\\c"), "a\\\"b\\\\c");
}

TEST(Logger, JsonEscapeNewline) {
    EXPECT_EQ(Logger::json_escape("line1\nline2"), "line1\\nline2");
}

TEST(Logger, JsonEscapeControlChar) {
    EXPECT_EQ(Logger::json_escape(std::string("\x01")), "\\u0001");
}

TEST(Logger, WriteLogEscapesInjection) {
    RunResult r;
    r.verdict = Verdict::AC;
    r.test_index = 1;
    r.compare_detail = "value has \"quote\" and \\slash";  // 可能的 JSON 注入

    const std::string path = ::testing::TempDir() + "cppjudge_test_log.json";
    ASSERT_TRUE(Logger::write_log(path, "p/dir", "s.cpp", Verdict::AC, {r}));

    std::ifstream f(path);
    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    EXPECT_NE(content.find("\"final_verdict\": \"Accepted\""), std::string::npos);
    // 引号/反斜杠被转义，不破坏 JSON
    EXPECT_NE(content.find("\\\"quote\\\""), std::string::npos);
    EXPECT_EQ(content.find("\"quote\""), std::string::npos);
}

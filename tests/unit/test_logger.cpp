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

TEST(Logger, JsonEscapeInvalidUtf8ByteReplaced) {
    // 孤立的 0x80 续接字节不是合法 UTF-8，须替换为 U+FFFD 转义，避免产出非法 UTF-8。
    EXPECT_EQ(Logger::json_escape(std::string("\x80")), "\\ufffd");
    // 截断的多字节序列（0xE4 后无续接）同样替换。
    EXPECT_EQ(Logger::json_escape(std::string("\xE4")), "\\ufffd");
}

TEST(Logger, JsonEscapeValidUtf8PassesThrough) {
    // 合法 UTF-8（“中” = E4 B8 AD）原样保留。
    const std::string zhong = "\xE4\xB8\xAD";
    EXPECT_EQ(Logger::json_escape(zhong), zhong);
}

TEST(Logger, JsonEscapeDelPassesThrough) {
    // 0x7F DEL 在 JSON 中合法，原样保留（不是 <0x20 控制字符）。
    EXPECT_EQ(Logger::json_escape(std::string("\x7F")), std::string("\x7F"));
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

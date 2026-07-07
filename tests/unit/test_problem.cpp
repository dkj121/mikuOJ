#include <gtest/gtest.h>

#include "cppjudge/problem.h"

using namespace cppjudge;

TEST(ProblemManager, LoadValid) {
    std::string err;
    auto p = ProblemManager::load("problems/A+B", err);
    ASSERT_NE(p, nullptr) << err;
    EXPECT_EQ(p->title, "A+B Problem");
    EXPECT_EQ(p->limits.cpu_time_ms, 2000u);
    EXPECT_EQ(p->limits.memory_mb, 256u);
    EXPECT_EQ(p->compare_mode, "exact");
    EXPECT_EQ(p->test_cases.size(), 2u);
}

TEST(ProblemManager, WallIsCpuTimesThree) {
    std::string err;
    auto p = ProblemManager::load("problems/A+B", err);
    ASSERT_NE(p, nullptr) << err;
    EXPECT_EQ(p->limits.wall_time_ms, p->limits.cpu_time_ms * 3);
}

TEST(ProblemManager, TestCasesPairedAndSorted) {
    std::string err;
    auto p = ProblemManager::load("problems/A+B", err);
    ASSERT_NE(p, nullptr) << err;
    EXPECT_EQ(p->test_cases[0].index, 1);
    EXPECT_EQ(p->test_cases[1].index, 2);
    EXPECT_NE(p->test_cases[0].input_file.find("1.in"), std::string::npos);
    EXPECT_NE(p->test_cases[0].output_file.find("1.out"), std::string::npos);
}

TEST(ProblemManager, ValidatePasses) {
    std::string err;
    auto p = ProblemManager::load("problems/A+B", err);
    ASSERT_NE(p, nullptr) << err;
    EXPECT_TRUE(ProblemManager::validate(*p, err)) << err;
}

TEST(ProblemManager, NonexistentDirFails) {
    std::string err;
    auto p = ProblemManager::load("problems/DoesNotExist", err);
    EXPECT_EQ(p, nullptr);
    EXPECT_FALSE(err.empty());
}

// ============================================================
// High-severity validation tests
// ============================================================

// H1: wall-time overflow guard — large values clamp instead of wrapping
TEST(ProblemManager, SafeWallTimeClampsOverflow) {
    // cpu_time_ms * 3 would overflow uint64_t
    constexpr uint64_t huge = UINT64_MAX / 2;  // *3 overflows
    uint64_t wall = ProblemManager::safe_wall_time(huge);
    EXPECT_GT(wall, 0u);                            // not zero (which would disable limit)
    EXPECT_LE(wall, ProblemManager::kMaxWallTimeMs); // clamped to max, not wrapped
    EXPECT_NE(wall, (huge * 3));                     // definitely not the overflowed result
}

TEST(ProblemManager, SafeWallTimeBelowThreshold) {
    EXPECT_EQ(ProblemManager::safe_wall_time(50000), 150000u);  // below max, normal *3
}

TEST(ProblemManager, SafeWallTimeNormal) {
    EXPECT_EQ(ProblemManager::safe_wall_time(1000), 3000u);
    EXPECT_EQ(ProblemManager::safe_wall_time(0), 0u);
}

// H2: upper-bound validation
TEST(ProblemManager, ValidateRejectsExcessiveCpuTime) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = ProblemManager::kMaxCpuTimeMs + 1;
    p.limits.memory_mb = 256;
    p.compare_mode = "exact";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("time_limit_ms"), std::string::npos);
}

TEST(ProblemManager, ValidateRejectsExcessiveMemory) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = ProblemManager::kMaxMemoryMb + 1;
    p.compare_mode = "exact";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("memory_limit_mb"), std::string::npos);
}

TEST(ProblemManager, ValidateRejectsExcessiveOutput) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = 256;
    p.limits.output_size_mb = ProblemManager::kMaxOutputMb + 1;
    p.compare_mode = "exact";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("output_limit_mb"), std::string::npos);
}

TEST(ProblemManager, ValidateRejectsExcessiveCompileTime) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = 256;
    p.limits.compile_time_ms = ProblemManager::kMaxCompileTimeMs + 1;
    p.compare_mode = "exact";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("compile_time_limit_ms"), std::string::npos);
}

// H3: sandbox_type validation
TEST(ProblemManager, ValidateRejectsInvalidSandboxType) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = 256;
    p.compare_mode = "exact";
    p.sandbox_type = "evil_backend";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("sandbox_type"), std::string::npos);
}

TEST(ProblemManager, ValidateAcceptsValidSandboxTypes) {
    for (const auto& st : {"auto", "builtin", "linux-ns"}) {
        Problem p;
        p.title = "test";
        p.limits.cpu_time_ms = 1000;
        p.limits.memory_mb = 256;
        p.compare_mode = "exact";
        p.sandbox_type = st;
        p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
        std::string err;
        EXPECT_TRUE(ProblemManager::validate(p, err)) << "sandbox_type=" << st << " should be valid";
    }
}

// H4: float tolerance negativity
TEST(ProblemManager, ValidateRejectsNegativeFloatAbsEps) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = 256;
    p.compare_mode = "floating";
    p.float_abs_eps = -0.1;
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("float_abs_eps"), std::string::npos);
}

TEST(ProblemManager, ValidateRejectsNegativeFloatRelEps) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = 256;
    p.compare_mode = "floating";
    p.float_rel_eps = -1e-3;
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("float_rel_eps"), std::string::npos);
}

TEST(ProblemManager, ValidateAcceptsZeroFloatEps) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = 256;
    p.compare_mode = "floating";
    p.float_abs_eps = 0.0;
    p.float_rel_eps = 0.0;
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_TRUE(ProblemManager::validate(p, err)) << "zero eps should be valid";
}

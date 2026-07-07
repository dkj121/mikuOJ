#include <gtest/gtest.h>

#include "cppjudge/problem.h"

#include <filesystem>
#include <fstream>

using namespace cppjudge;
namespace fs = std::filesystem;

// 用于 load() 测试的临时题目目录，析构时自动清理。
struct TempProblem {
    fs::path dir;
    explicit TempProblem(const std::string& label) {
        dir = fs::temp_directory_path() / ("mikuOJ_test_" + label);
        std::error_code ec;
        fs::remove_all(dir, ec);
        fs::create_directories(dir / "input", ec);
        fs::create_directories(dir / "output", ec);
    }
    ~TempProblem() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
    void json(const std::string& content) {
        std::ofstream f(dir / "problem.json");
        f << content;
    }
    void case_pair(const std::string& stem) {
        {
            std::ofstream f(dir / "input" / (stem + ".in"));
            f << "1 2\n";
        }
        {
            std::ofstream f(dir / "output" / (stem + ".out"));
            f << "3\n";
        }
    }
    void dir_output(const std::string& stem) {
        {
            std::ofstream f(dir / "input" / (stem + ".in"));
            f << "1 2\n";
        }
        fs::create_directory(dir / "output" / (stem + ".out"));
    }
};

// ---- 基本加载与校验 ----

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

// ---- safe_wall_time 溢出保护 ----

TEST(ProblemManager, SafeWallTimeClampsOverflow) {
    constexpr uint64_t huge = UINT64_MAX / 2;  // ×3 会溢出
    uint64_t wall = ProblemManager::safe_wall_time(huge);
    EXPECT_GT(wall, 0u);
    EXPECT_LE(wall, 180000u);
    EXPECT_NE(wall, (huge * 3));
}

TEST(ProblemManager, SafeWallTimeBelowThreshold) {
    EXPECT_EQ(ProblemManager::safe_wall_time(50000), 150000u);
}

TEST(ProblemManager, SafeWallTimeNormal) {
    EXPECT_EQ(ProblemManager::safe_wall_time(1000), 3000u);
    EXPECT_EQ(ProblemManager::safe_wall_time(0), 0u);
}

TEST(ProblemManager, ValidateRejectsZeroWallWithNonZeroCpu) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 2000;
    p.limits.memory_mb = 256;
    p.limits.wall_time_ms = 0;  // 显式设零绕过墙壁时间限制
    p.compare_mode = "exact";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("wall_time_ms"), std::string::npos);
}

// ---- validate: 上界拒绝 ----

TEST(ProblemManager, ValidateRejectsExcessiveCpuTime) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 60001;
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
    p.limits.memory_mb = 16385;
    p.compare_mode = "exact";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("memory_limit_mb"), std::string::npos);
}

TEST(ProblemManager, ValidateRejectsZeroOutputSize) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = 256;
    p.limits.output_size_mb = 0;
    p.compare_mode = "exact";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("output_limit_mb"), std::string::npos);
}

TEST(ProblemManager, ValidateRejectsExcessiveOutput) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = 256;
    p.limits.output_size_mb = 1025;
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
    p.limits.compile_time_ms = 120001;
    p.compare_mode = "exact";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("compile_time_limit_ms"), std::string::npos);
}

// ---- validate: 沙箱类型 ----

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
    for (const auto& st : {"auto", "linux-ns", "nsjail"}) {
        Problem p;
        p.title = "test";
        p.limits.cpu_time_ms = 1000;
        p.limits.memory_mb = 256;
        p.compare_mode = "exact";
        p.sandbox_type = st;
        p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
        std::string err;
        EXPECT_TRUE(ProblemManager::validate(p, err))
            << "sandbox_type=" << st << " should be valid";
    }
}

// ---- validate: 浮点容差 ----

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

// ---- validate: 栈与进程数 ----

TEST(ProblemManager, ValidateRejectsZeroStack) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = 256;
    p.limits.stack_mb = 0;
    p.compare_mode = "exact";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("stack_limit_mb"), std::string::npos);
}

TEST(ProblemManager, ValidateRejectsExcessiveStack) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = 256;
    p.limits.stack_mb = 4097;
    p.compare_mode = "exact";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("stack_limit_mb"), std::string::npos);
}

TEST(ProblemManager, ValidateRejectsZeroMaxProcesses) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = 256;
    p.limits.max_processes = 0;
    p.compare_mode = "exact";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("max_processes"), std::string::npos);
}

TEST(ProblemManager, ValidateRejectsExcessiveMaxProcesses) {
    Problem p;
    p.title = "test";
    p.limits.cpu_time_ms = 1000;
    p.limits.memory_mb = 256;
    p.limits.max_processes = 9999;
    p.compare_mode = "exact";
    p.test_cases = {{"/tmp/1.in", "/tmp/1.out", 1}};
    std::string err;
    EXPECT_FALSE(ProblemManager::validate(p, err));
    EXPECT_NE(err.find("max_processes"), std::string::npos);
}

// ---- 测试点文件名解析（通过 load 间接验证） ----

TEST(ProblemManager, AutoIndexForLeadingZeroStem) {
    TempProblem tp("zero");
    tp.json(R"({"title":"t","time_limit_ms":1000,"memory_limit_mb":64})");
    tp.case_pair("01");  // "01" 被拒绝 → 自动分配序号
    std::string err;
    auto p = ProblemManager::load(tp.dir.string(), err);
    ASSERT_NE(p, nullptr) << err;
    EXPECT_EQ(p->test_cases[0].index, 1);
}

TEST(ProblemManager, AutoIndexForNonNumericStem) {
    TempProblem tp("alpha");
    tp.json(R"({"title":"t","time_limit_ms":1000,"memory_limit_mb":64})");
    tp.case_pair("abc");
    std::string err;
    auto p = ProblemManager::load(tp.dir.string(), err);
    ASSERT_NE(p, nullptr) << err;
    EXPECT_EQ(p->test_cases[0].index, 1);
}

TEST(ProblemManager, AutoIndexForPartialParseStem) {
    TempProblem tp("mixed");
    tp.json(R"({"title":"t","time_limit_ms":1000,"memory_limit_mb":64})");
    tp.case_pair("123abc");  // 部分数字拒绝
    std::string err;
    auto p = ProblemManager::load(tp.dir.string(), err);
    ASSERT_NE(p, nullptr) << err;
    EXPECT_EQ(p->test_cases[0].index, 1);
}

// ---- S_ISREG 拒绝目录 ----

TEST(ProblemManager, RejectsDirectoryAsOutputFile) {
    TempProblem tp("dirout");
    tp.json(R"({"title":"t","time_limit_ms":1000,"memory_limit_mb":64})");
    tp.dir_output("1");  // output/1.out/ 是目录而非文件
    std::string err;
    auto p = ProblemManager::load(tp.dir.string(), err);
    EXPECT_EQ(p, nullptr);
    EXPECT_NE(err.find("not a regular file"), std::string::npos);
}

// ---- JSON 可选字段加载 ----

TEST(ProblemManager, LoadsExplicitWallTimeMs) {
    TempProblem tp("wall");
    tp.json(R"({
      "title":"w","time_limit_ms":1000,"memory_limit_mb":64,
      "wall_time_ms": 7777
    })");
    tp.case_pair("1");
    std::string err;
    auto p = ProblemManager::load(tp.dir.string(), err);
    ASSERT_NE(p, nullptr) << err;
    EXPECT_EQ(p->limits.wall_time_ms, 7777u);
}

TEST(ProblemManager, LoadsStackLimitMb) {
    TempProblem tp("stack");
    tp.json(R"({
      "title":"s","time_limit_ms":1000,"memory_limit_mb":64,
      "stack_limit_mb": 32
    })");
    tp.case_pair("1");
    std::string err;
    auto p = ProblemManager::load(tp.dir.string(), err);
    ASSERT_NE(p, nullptr) << err;
    EXPECT_EQ(p->limits.stack_mb, 32u);
}

TEST(ProblemManager, LoadsMaxProcesses) {
    TempProblem tp("proc");
    tp.json(R"({
      "title":"p","time_limit_ms":1000,"memory_limit_mb":64,
      "max_processes": 16
    })");
    tp.case_pair("1");
    std::string err;
    auto p = ProblemManager::load(tp.dir.string(), err);
    ASSERT_NE(p, nullptr) << err;
    EXPECT_EQ(p->limits.max_processes, 16u);
}

TEST(ProblemManager, LoadsDefaultsWhenFieldsMissing) {
    TempProblem tp("defaults");
    tp.json(R"({"title":"d","time_limit_ms":1000,"memory_limit_mb":64})");
    tp.case_pair("1");
    std::string err;
    auto p = ProblemManager::load(tp.dir.string(), err);
    ASSERT_NE(p, nullptr) << err;
    EXPECT_EQ(p->limits.stack_mb, 8u);
    EXPECT_EQ(p->limits.max_processes, 64u);
    EXPECT_EQ(p->limits.wall_time_ms, 3000u);
}

TEST(ProblemManager, LoadsExplicitZeroWallTime) {
    TempProblem tp("zerowall");
    tp.json(R"({"title":"z","time_limit_ms":2000,"memory_limit_mb":64,"wall_time_ms":0})");
    tp.case_pair("1");
    std::string err;
    auto p = ProblemManager::load(tp.dir.string(), err);
    ASSERT_NE(p, nullptr) << err;
    EXPECT_EQ(p->limits.wall_time_ms, 0u);
}

TEST(ProblemManager, LoadFailsOnWallTimeMsTypeError) {
    TempProblem tp("badwall");
    tp.json(R"({"title":"b","time_limit_ms":1000,"memory_limit_mb":64,"wall_time_ms":"abc"})");
    tp.case_pair("1");
    std::string err;
    auto p = ProblemManager::load(tp.dir.string(), err);
    EXPECT_EQ(p, nullptr);
    EXPECT_NE(err.find("wall_time_ms"), std::string::npos);
}

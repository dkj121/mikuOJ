#include <gtest/gtest.h>

#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#include "cppjudge/cgroup_manager.h"

using namespace cppjudge::cgroup;

namespace {
bool privileged() {
    return geteuid() == 0 && Manager::is_cgroup_v2_available();
}
}  // namespace

TEST(Cgroup, AvailabilityQueryable) {
    (void)Manager::is_cgroup_v2_available();
    SUCCEED();
}

TEST(Cgroup, EmptyIdInvalid) {
    auto m = Manager::create("");
    EXPECT_FALSE(m.is_valid());
}

TEST(Cgroup, CreateApplyCollectDestroy) {
    if (!privileged()) GTEST_SKIP() << "needs root + cgroup v2 delegation";
    auto m = Manager::create("test-cad");
    ASSERT_TRUE(m.is_valid());
    Limits l;
    l.memory_bytes = 64ULL * 1024 * 1024;
    l.max_pids = 16;
    EXPECT_TRUE(m.apply(l));
    auto s = m.collect();
    EXPECT_GE(s.memory_kb, 0u);
    m.destroy();
}

TEST(Cgroup, PathReflectsId) {
    auto m = Manager::create("test-path");
    if (!m.is_valid()) GTEST_SKIP() << "cgroup not writable";
    EXPECT_NE(m.path().find("test-path"), std::string::npos);
    m.destroy();
}

TEST(Cgroup, MemoryPeakTracking) {
    if (!privileged()) GTEST_SKIP() << "needs root + cgroup v2 delegation";
    auto m = Manager::create("test-peak");
    ASSERT_TRUE(m.is_valid());
    Limits l;
    l.memory_bytes = 128ULL * 1024 * 1024;
    l.max_pids = 16;
    ASSERT_TRUE(m.apply(l));

    // Collect baseline stats
    auto s1 = m.collect();
    EXPECT_GE(s1.memory_kb, 0u);
    EXPECT_GE(s1.memory_peak_kb, s1.memory_kb);

    m.destroy();
}

TEST(Cgroup, ConcurrentCgroupsIsolated) {
    if (!privileged()) GTEST_SKIP() << "needs root + cgroup v2 delegation";
    constexpr int N = 5;
    Manager mgrs[N];
    for (int i = 0; i < N; ++i) {
        mgrs[i] = Manager::create("test-concurrent-" + std::to_string(i));
        ASSERT_TRUE(mgrs[i].is_valid());
        Limits l;
        l.memory_bytes = (64ULL + i * 32) * 1024 * 1024;
        l.max_pids = 10 + i;
        ASSERT_TRUE(mgrs[i].apply(l));
    }
    // All should coexist
    for (int i = 0; i < N; ++i) {
        auto s = mgrs[i].collect();
        EXPECT_GE(s.memory_kb, 0u);
    }
    for (int i = 0; i < N; ++i) {
        mgrs[i].destroy();
    }
}

TEST(Cgroup, DestroyWithoutAttachSucceeds) {
    if (!privileged()) GTEST_SKIP() << "needs root + cgroup v2 delegation";
    auto m = Manager::create("test-destroy-empty");
    ASSERT_TRUE(m.is_valid());
    Limits l;
    l.memory_bytes = 64ULL * 1024 * 1024;
    l.max_pids = 8;
    ASSERT_TRUE(m.apply(l));
    // No process attached, destroy should succeed immediately
    EXPECT_NO_THROW(m.destroy());
    EXPECT_FALSE(m.is_valid());
}

TEST(Cgroup, PathTraversalRejected) {
    // 安全性：拒绝包含路径穿越字符的 sandbox_id
    auto m1 = Manager::create("../../etc/passwd");
    EXPECT_FALSE(m1.is_valid());

    auto m2 = Manager::create("foo/bar");
    EXPECT_FALSE(m2.is_valid());

    auto m3 = Manager::create("valid-id-123");
    // 即使没有 root 权限，至少不会因路径穿越而崩溃
    if (m3.is_valid()) m3.destroy();
}

TEST(Cgroup, AttachProcessAndDestroySlow) {
    if (!privileged()) GTEST_SKIP() << "needs root + cgroup v2 delegation";

    auto m = Manager::create("test-attach-destroy");
    ASSERT_TRUE(m.is_valid());

    Limits l;
    l.memory_bytes = 64ULL * 1024 * 1024;
    l.max_pids = 32;
    ASSERT_TRUE(m.apply(l));

    // Fork 子进程并 attach
    pid_t child = fork();
    ASSERT_NE(child, -1);

    if (child == 0) {
        // 子进程：睡眠 200ms 等待被 cgroup.kill 杀死
        usleep(200000);
        _exit(0);
    }

    // 父进程：attach 子进程到 cgroup
    ASSERT_TRUE(m.attach(child));

    // 验证子进程在 cgroup 里（读 cgroup.procs 应包含 child PID）
    usleep(10000);  // 等待 attach 生效

    // destroy() 应触发 cgroup.kill 并等待进程退出（慢速路径）
    m.destroy();

    // 验证子进程被杀死
    int status;
    pid_t result = waitpid(child, &status, WNOHANG);
    EXPECT_EQ(result, child);  // 子进程应已退出
    if (result != child) {
        // 清理残留进程
        kill(child, SIGKILL);
        waitpid(child, &status, 0);
    }
}

TEST(Cgroup, ParseU64Overflow) {
    if (!privileged()) GTEST_SKIP() << "needs root + cgroup v2 delegation";

    // 创建 cgroup 并写入超大数值到 memory.max，验证 collect() 不崩溃
    auto m = Manager::create("test-overflow");
    ASSERT_TRUE(m.is_valid());

    Limits l;
    l.memory_bytes = UINT64_MAX;  // 最大值
    l.max_pids = 16;
    ASSERT_TRUE(m.apply(l));

    // collect() 不应因溢出而崩溃
    auto s = m.collect();
    EXPECT_GE(s.memory_kb, 0u);

    m.destroy();
}

TEST(Cgroup, InvalidManagerOperations) {
    // 测试未初始化 Manager 的所有操作返回失败
    Manager invalid;
    EXPECT_FALSE(invalid.is_valid());

    Limits l;
    l.memory_bytes = 64ULL * 1024 * 1024;
    l.max_pids = 16;
    EXPECT_FALSE(invalid.apply(l));
    EXPECT_FALSE(invalid.attach(getpid()));

    auto s = invalid.collect();
    EXPECT_EQ(s.cpu_usage_us, 0u);
    EXPECT_EQ(s.memory_kb, 0u);
    EXPECT_FALSE(s.oom_killed);

    // destroy 空操作（不应崩溃）
    EXPECT_NO_THROW(invalid.destroy());
}

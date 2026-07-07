#include <gtest/gtest.h>

#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>

#include "cppjudge/ns_manager.h"

using namespace cppjudge::ns;

TEST(NsManager, MountEntryDefaultsReadOnly) {
    MountEntry e{"/usr/bin/g++", "/usr/bin/g++"};
    EXPECT_FALSE(e.writable);
}

TEST(NsManager, MountEntryWritable) {
    MountEntry e{"/tmp/work", "/box", true};
    EXPECT_TRUE(e.writable);
}

TEST(NsManager, AllNsFlagsCoverExpected) {
    EXPECT_TRUE(Manager::ALL_NS_FLAGS & CLONE_NEWNS);
    EXPECT_TRUE(Manager::ALL_NS_FLAGS & CLONE_NEWPID);
    EXPECT_TRUE(Manager::ALL_NS_FLAGS & CLONE_NEWNET);
    EXPECT_TRUE(Manager::ALL_NS_FLAGS & CLONE_NEWIPC);
    EXPECT_TRUE(Manager::ALL_NS_FLAGS & CLONE_NEWUTS);
}

TEST(NsManager, CloneAndExecSpawnsChild) {
    if (geteuid() != 0) GTEST_SKIP() << "clone(NEWPID/NEWNET) needs privilege";
    pid_t child = Manager::clone_and_exec(
        CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWIPC | CLONE_NEWUTS,
        []() -> int { _exit(42); });
    ASSERT_GT(child, 0);
    int status = 0;
    waitpid(child, &status, 0);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 42);
}

TEST(NsManager, CloneRejectsBadFlags) {
    pid_t child = Manager::clone_and_exec(-1, []() -> int { _exit(0); });
    EXPECT_EQ(child, -1);
}

TEST(NsManager, BindMinimalDevicesCreatesDevNodes) {
    if (geteuid() != 0) GTEST_SKIP() << "bind_minimal_devices needs privilege";

    // 创建临时根目录
    char temp_root[] = "/tmp/test_ns_root_XXXXXX";
    ASSERT_NE(mkdtemp(temp_root), nullptr);

    SetupResult result = Manager::bind_minimal_devices(temp_root);
    EXPECT_TRUE(result.ok) << result.error;

    // 验证关键设备节点和符号链接存在
    std::string dev_dir = std::string(temp_root) + "/dev";
    struct stat st;

    // 检查设备节点
    EXPECT_EQ(stat((dev_dir + "/null").c_str(), &st), 0);
    EXPECT_EQ(stat((dev_dir + "/zero").c_str(), &st), 0);
    EXPECT_EQ(stat((dev_dir + "/urandom").c_str(), &st), 0);

    // 检查符号链接（使用 lstat 不跟随链接）
    EXPECT_EQ(lstat((dev_dir + "/fd").c_str(), &st), 0);
    EXPECT_TRUE(S_ISLNK(st.st_mode));
    EXPECT_EQ(lstat((dev_dir + "/stdin").c_str(), &st), 0);
    EXPECT_TRUE(S_ISLNK(st.st_mode));

    // 清理
    umount2(dev_dir.c_str(), MNT_DETACH);
    rmdir(temp_root);
}

TEST(NsManager, SetupRootfsFailsOnInvalidPath) {
    if (geteuid() != 0) GTEST_SKIP() << "setup_rootfs needs privilege";

    // 空的挂载列表，但使用无效路径（不可写的路径）
    std::vector<MountEntry> entries;
    std::string invalid_root = "/proc/invalid_cannot_create";

    SetupResult result = Manager::setup_rootfs(entries, invalid_root);
    EXPECT_FALSE(result.ok);  // 应该失败
    EXPECT_FALSE(result.error.empty());
}

TEST(NsManager, DropPrivilegesFailsWhenNotRoot) {
    if (geteuid() == 0) GTEST_SKIP() << "test requires non-root user";

    // 非 root 用户调用应该失败
    bool result = Manager::drop_privileges();
    EXPECT_FALSE(result);
}

TEST(NsManager, CloneAndExecHandlesChildFailure) {
    if (geteuid() != 0) GTEST_SKIP() << "clone(NEWPID/NEWNET) needs privilege";

    // 子进程以非零状态退出
    pid_t child = Manager::clone_and_exec(
        CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWIPC | CLONE_NEWUTS,
        []() -> int { _exit(123); });

    ASSERT_GT(child, 0);
    int status = 0;
    waitpid(child, &status, 0);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 123);
}

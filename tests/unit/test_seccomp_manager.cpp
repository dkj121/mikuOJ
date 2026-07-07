#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include "cppjudge/seccomp_manager.h"

using namespace cppjudge;
using namespace cppjudge::seccomp;

namespace {
bool contains(const std::vector<std::string>& v, const std::string& n) {
    return std::find(v.begin(), v.end(), n) != v.end();
}
}  // namespace

TEST(Seccomp, ProfileForLang) {
    EXPECT_EQ(profile_for_lang("cpp"), SeccompProfile::Strict);
    EXPECT_EQ(profile_for_lang("c"), SeccompProfile::Strict);
    EXPECT_EQ(profile_for_lang("go"), SeccompProfile::Standard);
    EXPECT_EQ(profile_for_lang("rust"), SeccompProfile::Standard);
    EXPECT_EQ(profile_for_lang("python3"), SeccompProfile::Extended);
    EXPECT_EQ(profile_for_lang("java"), SeccompProfile::JVM);
    EXPECT_EQ(profile_for_lang("brainfuck"), SeccompProfile::Strict);  // 未知 → 最安全
}

TEST(Seccomp, AllowlistsNested) {
    const auto& s = Manager::allowlist_for_testing(SeccompProfile::Strict);
    const auto& st = Manager::allowlist_for_testing(SeccompProfile::Standard);
    const auto& e = Manager::allowlist_for_testing(SeccompProfile::Extended);
    const auto& j = Manager::allowlist_for_testing(SeccompProfile::JVM);
    EXPECT_GT(s.size(), 0u);
    EXPECT_GE(st.size(), s.size());
    EXPECT_GE(e.size(), st.size());
    EXPECT_GE(j.size(), e.size());
}

TEST(Seccomp, AllProfilesAllowExecve) {
    for (auto p : {SeccompProfile::Strict, SeccompProfile::Standard,
                   SeccompProfile::Extended, SeccompProfile::JVM}) {
        const auto& l = Manager::allowlist_for_testing(p);
        EXPECT_TRUE(contains(l, "execve"));
        EXPECT_TRUE(contains(l, "execveat"));
    }
}

TEST(Seccomp, PtraceBlockedEverywhere) {
    for (auto p : {SeccompProfile::Strict, SeccompProfile::Standard,
                   SeccompProfile::Extended, SeccompProfile::JVM}) {
        EXPECT_FALSE(contains(Manager::allowlist_for_testing(p), "ptrace"));
    }
}

TEST(Seccomp, NetworkBlockedExceptJvmLocalSockets) {
    // Strict/Standard/Extended 禁 socket/connect。JVM 例外：需本地 socket，
    // 真正的网络隔离由空 net namespace(CLONE_NEWNET) 保证。
    for (auto p : {SeccompProfile::Strict, SeccompProfile::Standard,
                   SeccompProfile::Extended}) {
        const auto& l = Manager::allowlist_for_testing(p);
        EXPECT_FALSE(contains(l, "socket"));
        EXPECT_FALSE(contains(l, "connect"));
    }
}

TEST(Seccomp, StrictAllowsSignalSelfDelivery) {
    // glibc 的 raise()/abort()/断言失败经 tgkill 投递 SIGABRT；缺失会导致正常的
    // C/C++ 运行时错误被 seccomp 以 SIGSYS 杀死，误判为 SV 而非 RE。
    const auto& s = Manager::allowlist_for_testing(SeccompProfile::Strict);
    EXPECT_TRUE(contains(s, "tgkill"));
    EXPECT_TRUE(contains(s, "tkill"));
    // 阻塞型系统调用被信号中断后内核用 restart_syscall 续跑。
    EXPECT_TRUE(contains(s, "restart_syscall"));
}

TEST(Seccomp, SignalSyscallsPresentInAllProfiles) {
    // Strict 已含，嵌套的 Standard/Extended/JVM 也应继承（不得回退）。
    for (auto p : {SeccompProfile::Strict, SeccompProfile::Standard,
                   SeccompProfile::Extended, SeccompProfile::JVM}) {
        const auto& l = Manager::allowlist_for_testing(p);
        EXPECT_TRUE(contains(l, "tgkill")) << "profile missing tgkill";
        EXPECT_TRUE(contains(l, "restart_syscall")) << "profile missing restart_syscall";
    }
}

TEST(Seccomp, ViolationToString) {
    // syscall 0 在两个架构上都存在（x86_64=read, aarch64=io_setup），名称非空即可
    EXPECT_FALSE(Manager::violation_to_string(0).empty());
}

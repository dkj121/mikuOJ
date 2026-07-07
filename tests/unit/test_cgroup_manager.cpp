#include <gtest/gtest.h>

#include <unistd.h>

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

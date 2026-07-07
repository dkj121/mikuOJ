#pragma once

// Linux 专用：cgroup v2 资源限制与计量。

#include <sys/types.h>

#include <cstdint>
#include <string>

namespace cppjudge::cgroup {

struct Limits {
    uint64_t cpu_time_us  = 0;   // 参考值；真正的 CPU-TLE 由 Sandbox 采样 + kill 实现（D7）
    uint64_t memory_bytes = 0;
    uint64_t max_pids     = 0;
};

struct Stats {
    uint64_t cpu_usage_us   = 0;
    uint64_t memory_kb      = 0;
    uint64_t memory_peak_kb = 0;
    bool     oom_killed     = false;
};

class Manager {
public:
    static bool is_cgroup_v2_available();

    // 创建叶子 cgroup：自动建 /sys/fs/cgroup/cppjudge 父层级并在 subtree_control
    // 委派 memory/pids/cpu 控制器（修 D1）。失败返回 is_valid()==false。
    static Manager create(const std::string& sandbox_id);

    bool               is_valid() const { return valid_; }
    const std::string& path() const { return path_; }  // 供 CLONE_INTO_CGROUP 打开目录 fd
    bool               apply(const Limits& limits);
    bool               attach(pid_t pid);
    Stats              collect() const;
    void               destroy();

private:
    bool        write_control(const std::string& file, const std::string& value) const;
    std::string read_control(const std::string& file) const;

    std::string path_;
    bool        valid_ = false;
};

} // namespace cppjudge::cgroup

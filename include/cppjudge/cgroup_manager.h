#pragma once

// Linux 专用：cgroup v2 资源限制与计量。
//
// 线程安全性：Manager 对象本身非线程安全，多线程环境需外部同步。
// 不同 Manager 实例（不同 sandbox_id）可并发操作。

#include <sys/types.h>

#include <cstdint>
#include <string>

namespace cppjudge::cgroup {

/// 资源限制配置。传递给 Manager::apply()。
struct Limits {
    uint64_t cpu_time_us  = 0;   // 参考值；真正的 CPU-TLE 由 Sandbox 采样 + kill 实现（D7）
    uint64_t memory_bytes = 0;   // 物理内存上限（字节）；0 表示不限制
    uint64_t max_pids     = 0;   // 进程/线程数上限；0 表示不限制
};

/// 资源使用统计。由 Manager::collect() 返回。
struct Stats {
    uint64_t cpu_usage_us   = 0;   // 累计 CPU 时间（微秒），单调递增
    uint64_t memory_kb      = 0;   // 当前内存使用（KB）
    uint64_t memory_peak_kb = 0;   // 峰值内存使用（KB），Linux 5.19+ 可用
    bool     oom_killed     = false; // 是否触发 OOM killer
};

/// Cgroup v2 资源管理器。
///
/// 使用模式：
///   1. Manager::create(id) 创建 cgroup 层级
///   2. apply(limits) 设置资源限制
///   3. attach(pid) 将进程加入 cgroup
///   4. collect() 读取资源统计
///   5. destroy() 清理（杀死所有进程并删除 cgroup）
///
/// 前置条件：
///   - 必须以 root 运行（cgroup 写入权限）
///   - 内核 5.15+ 且 cgroup v2 已挂载到 /sys/fs/cgroup
///   - /sys/fs/cgroup/cppjudge 父层级的 subtree_control 已委派 cpu/memory/pids 控制器
class Manager {
public:
    /// 检测系统是否支持 cgroup v2。
    ///
    /// @return true 如果 /sys/fs/cgroup 挂载了 cgroup2 且可写。
    static bool is_cgroup_v2_available();

    /// 创建叶子 cgroup。自动建 /sys/fs/cgroup/cppjudge 父层级并在 subtree_control
    /// 委派 memory/pids/cpu 控制器（修 D1）。
    ///
    /// @param sandbox_id  沙箱唯一标识（用于路径：/sys/fs/cgroup/cppjudge/<sandbox_id>）
    /// @return            Manager 实例。失败时 is_valid()==false
    ///
    /// 线程安全性：不同 sandbox_id 可并发调用。相同 sandbox_id 并发调用是未定义行为。
    static Manager create(const std::string& sandbox_id);

    /// 检查 Manager 是否有效（创建成功）。
    ///
    /// @return true 如果 cgroup 已成功创建且未 destroy()。
    bool is_valid() const { return valid_; }

    /// 获取 cgroup 在文件系统中的路径。
    ///
    /// @return 绝对路径（例：/sys/fs/cgroup/cppjudge/sandbox-123）
    ///
    /// 用途：供 CLONE_INTO_CGROUP 打开目录 fd。
    const std::string& path() const { return path_; }

    /// 应用资源限制。
    ///
    /// @param limits  限制配置
    /// @return        true 如果所有限制写入成功
    ///
    /// 前置条件：is_valid() == true
    /// 副作用：写入 memory.max / memory.swap.max / pids.max 控制文件
    bool apply(const Limits& limits);

    /// 将进程加入此 cgroup。
    ///
    /// @param pid  目标进程 PID
    /// @return     true 如果成功写入 cgroup.procs
    ///
    /// 前置条件：is_valid() == true，pid 存在且当前用户有权限操作
    /// 副作用：进程的资源使用立即受此 cgroup 限制
    bool attach(pid_t pid);

    /// 收集当前资源使用统计。
    ///
    /// @return Stats 结构体。失败时所有字段为 0/false
    ///
    /// 前置条件：is_valid() == true
    /// 数据来源：cpu.stat、memory.current、memory.peak、memory.events
    Stats collect() const;

    /// 销毁 cgroup：杀死所有进程并删除目录。
    ///
    /// 副作用：
    ///   1. 写入 cgroup.kill=1（发送 SIGKILL 给所有进程）
    ///   2. 轮询 cgroup.procs 直到为空（最多 1 秒）
    ///   3. rmdir() 删除 cgroup 目录
    ///   4. 对象失效（is_valid() → false）
    ///
    /// 注意：destroy() 后不可再调用 apply/attach/collect。
    /// 幂等性：重复调用安全（第二次为空操作）。
    void destroy();

private:
    bool        write_control(const std::string& file, const std::string& value) const;
    std::string read_control(const std::string& file) const;

    std::string path_;       // cgroup 绝对路径
    bool        valid_ = false;  // 标记对象是否已成功创建
};

} // namespace cppjudge::cgroup

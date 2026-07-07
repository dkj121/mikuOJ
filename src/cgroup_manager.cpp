#include "cppjudge/cgroup_manager.h"

#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <cerrno>
#include <fstream>
#include <iterator>
#include <sstream>

namespace cppjudge::cgroup {

namespace {

constexpr const char* kRoot   = "/sys/fs/cgroup";
constexpr const char* kParent = "/sys/fs/cgroup/cppjudge";

std::string read_all(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(f)),
                       std::istreambuf_iterator<char>());
}

// 安全解析 u64（空/非法 → 0，溢出 → UINT64_MAX，绝不抛异常，修 D15）。
uint64_t parse_u64(const std::string& s) {
    uint64_t v = 0;
    bool any = false;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            uint64_t digit = static_cast<uint64_t>(c - '0');
            // 溢出检测：如果 v * 10 + digit > UINT64_MAX，饱和到最大值
            if (v > (UINT64_MAX - digit) / 10) {
                return UINT64_MAX;
            }
            v = v * 10 + digit;
            any = true;
        } else if (any) {
            break;
        }
    }
    return any ? v : 0;
}

// 在 dir/cgroup.subtree_control 委派可用控制器。
void enable_controllers(const std::string& dir) {
    const std::string avail = read_all(dir + "/cgroup.controllers");
    std::ofstream sub(dir + "/cgroup.subtree_control");
    if (!sub.is_open()) return;
    for (const char* c : {"memory", "pids", "cpu"}) {
        if (avail.find(c) != std::string::npos) sub << "+" << c << " ";
    }
}

bool controllers_delegated(const std::string& dir) {
    return read_all(dir + "/cgroup.subtree_control").find("memory") != std::string::npos;
}

// 把 from 目录里的进程全部迁到 to（用于嵌套 cgroup：root 有进程时无法委派控制器）。
void migrate_procs(const std::string& from_dir, const std::string& to_dir) {
    std::ifstream in(from_dir + "/cgroup.procs");
    std::string pid;
    while (std::getline(in, pid)) {
        if (pid.empty()) continue;
        std::ofstream o(to_dir + "/cgroup.procs");  // 逐个写（cgroup.procs 一次一个 pid）
        if (o.is_open()) o << pid;
    }
}

} // namespace

bool Manager::is_cgroup_v2_available() {
    std::ifstream mounts("/proc/mounts");
    if (!mounts.is_open()) return false;
    std::string line;
    while (std::getline(mounts, line)) {
        if (line.find("cgroup2") != std::string::npos &&
            line.find(kRoot) != std::string::npos) {
            return true;
        }
    }
    return false;
}

Manager Manager::create(const std::string& sandbox_id) {
    Manager m;
    if (sandbox_id.empty()) return m;
    // 防止路径穿越（安全性：拒绝包含 / 或 .. 的 sandbox_id）
    if (sandbox_id.find('/') != std::string::npos ||
        sandbox_id.find("..") != std::string::npos) {
        return m;  // valid_ 保持 false
    }

    // 建父层级并委派控制器。真机 root 是 "no internal processes" 规则的例外，可直接委派。
    mkdir(kParent, 0755);  // 忽略 EEXIST
    enable_controllers(kRoot);
    if (!controllers_delegated(kRoot)) {
        // 嵌套/容器场景：root 有进程 → 迁到 _init 叶子后再委派。
        const std::string init = std::string(kParent) + "/_init";
        mkdir(init.c_str(), 0755);
        migrate_procs(kRoot, init);
        enable_controllers(kRoot);
    }
    enable_controllers(kParent);

    m.path_ = std::string(kParent) + "/" + sandbox_id;
    if (mkdir(m.path_.c_str(), 0755) != 0 && errno != EEXIST) {
        return m;  // valid_ 保持 false
    }
    m.valid_ = true;
    return m;
}

bool Manager::write_control(const std::string& file, const std::string& value) const {
    if (!valid_) return false;
    std::ofstream f(path_ + "/" + file);
    if (!f.is_open()) return false;
    f << value;
    return f.good();
}

std::string Manager::read_control(const std::string& file) const {
    if (!valid_) return "";
    return read_all(path_ + "/" + file);
}

bool Manager::apply(const Limits& limits) {
    if (!valid_) return false;
    bool ok = true;
    if (limits.memory_bytes > 0) {
        ok = write_control("memory.max", std::to_string(limits.memory_bytes)) && ok;
        ok = write_control("memory.swap.max", "0") && ok;  // 禁用 swap
    }
    if (limits.max_pids > 0) {
        ok = write_control("pids.max", std::to_string(limits.max_pids)) && ok;
    }
    // cpu.max 不设硬节流；CPU-TLE 由 Sandbox 采样 usage_usec + 主动 kill（修 D7）。
    return ok;
}

bool Manager::attach(pid_t pid) {
    if (!valid_) return false;
    return write_control("cgroup.procs", std::to_string(pid));
}

Stats Manager::collect() const {
    Stats s;
    if (!valid_) return s;

    {
        std::istringstream is(read_control("cpu.stat"));
        std::string key;
        uint64_t val;
        while (is >> key >> val) {
            if (key == "usage_usec") { s.cpu_usage_us = val; break; }
        }
    }

    s.memory_kb = parse_u64(read_control("memory.current")) / 1024;

    const std::string peak = read_control("memory.peak");  // 5.19+ 才有
    s.memory_peak_kb = peak.empty() ? s.memory_kb : parse_u64(peak) / 1024;

    {
        std::istringstream is(read_control("memory.events"));
        std::string key;
        uint64_t val;
        while (is >> key >> val) {
            if (key == "oom_kill" && val > 0) { s.oom_killed = true; break; }
        }
    }
    return s;
}

void Manager::destroy() {
    if (!valid_) return;
    write_control("cgroup.kill", "1");
    // 快速路径：大部分情况下 cgroup 为空或立即清空，先检查一次
    if (read_control("cgroup.procs").empty()) {
        rmdir(path_.c_str());
        valid_ = false;
        return;
    }
    // 慢速路径：轮询直到无进程再 rmdir（修 D14：kill 是异步的，直接 rmdir 会 EBUSY）
    for (int i = 0; i < 200; ++i) {
        struct timespec ts{0, 5 * 1000 * 1000};  // 5ms
        nanosleep(&ts, nullptr);
        if (read_control("cgroup.procs").empty()) break;
    }
    rmdir(path_.c_str());
    valid_ = false;
}

} // namespace cppjudge::cgroup

#include "cppjudge/ns_manager.h"

#include <fcntl.h>
#include <grp.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>  // makedev
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#if defined(__has_include)
#if __has_include(<linux/mount.h>)
#include <linux/mount.h>  // mount_setattr / MOUNT_ATTR_*
#endif
#endif

namespace cppjudge::ns {

namespace {

constexpr size_t kChildStackSize = 1024 * 1024;
constexpr uid_t  kNobodyUid = 65534;
constexpr gid_t  kNobodyGid = 65534;

bool mkdir_p(const std::string& path, mode_t mode = 0755) {
    if (path.empty() || path == "/") return true;
    size_t pos = 0;
    while (pos != std::string::npos) {
        pos = path.find('/', pos + 1);
        std::string dir = path.substr(0, pos);
        if (dir.empty()) continue;
        if (mkdir(dir.c_str(), mode) != 0 && errno != EEXIST) return false;
    }
    return true;
}

// 递归只读 + nosuid/nodev（优先 mount_setattr，回退 remount）。修 D19。
bool remount_readonly(const std::string& target) {
#if defined(MOUNT_ATTR_RDONLY) && defined(AT_RECURSIVE)
    struct mount_attr attr{};
    attr.attr_set = MOUNT_ATTR_RDONLY | MOUNT_ATTR_NOSUID | MOUNT_ATTR_NODEV;
    int fd = open(target.c_str(), O_PATH | O_CLOEXEC);
    if (fd >= 0) {
        long rc = syscall(SYS_mount_setattr, fd, "", AT_EMPTY_PATH | AT_RECURSIVE,
                          &attr, sizeof(attr));
        close(fd);
        if (rc == 0) return true;
    }
#endif
    // 回退：非递归 remount（对子挂载不完全，best-effort）
    unsigned long flags = MS_BIND | MS_REMOUNT | MS_RDONLY | MS_NOSUID | MS_NODEV | MS_REC;
    return mount(nullptr, target.c_str(), nullptr, flags, nullptr) == 0;
}

} // namespace

bool Manager::make_readonly(const std::string& target) {
    return remount_readonly(target);
}

bool Manager::bind_mount_one(const std::string& source,
                             const std::string& target,
                             bool writable) {
    struct stat st{};
    if (stat(source.c_str(), &st) != 0) {
        return false;  // 源不存在 → 调用方决定是否跳过
    }

    if (S_ISDIR(st.st_mode)) {
        if (!mkdir_p(target)) return false;
    } else {
        std::string parent = target.substr(0, target.rfind('/'));
        if (!mkdir_p(parent)) return false;
        int fd = open(target.c_str(), O_CREAT | O_WRONLY, 0644);
        if (fd >= 0) close(fd);
    }

    if (mount(source.c_str(), target.c_str(), nullptr, MS_BIND | MS_REC, nullptr) != 0) {
        return false;
    }
    if (!writable) {
        return make_readonly(target);
    }
    return true;
}

SetupResult Manager::setup_rootfs(const std::vector<MountEntry>& entries,
                                  const std::string& new_root) {
    SetupResult r;
    auto fail = [&](const std::string& what) {
        r.ok = false;
        r.error = what + ": " + std::strerror(errno);
        return r;
    };

    // 1. / 设为私有，避免挂载传播回宿主
    if (mount(nullptr, "/", nullptr, MS_PRIVATE | MS_REC, nullptr) != 0) {
        return fail("make / private");
    }

    // 2. 唯一私有 new_root 上挂 tmpfs（nosuid/nodev）
    if (!mkdir_p(new_root)) return fail("mkdir new_root");
    if (mount("tmpfs", new_root.c_str(), "tmpfs",
              MS_NOSUID | MS_NODEV, "size=256m,mode=755") != 0) {
        return fail("mount tmpfs on new_root");
    }

    // 3. bind 白名单条目（源缺失则跳过并告警，非致命；修多架构路径）
    for (const auto& e : entries) {
        std::string target = new_root + e.target;
        struct stat st{};
        if (stat(e.source.c_str(), &st) != 0) {
            continue;  // 该依赖在本机不存在 → 跳过
        }
        if (!bind_mount_one(e.source, target, e.writable)) {
            return fail("bind mount " + e.source + " -> " + target);
        }
        if (e.writable) {
            chmod(target.c_str(), 0777);  // 供降权后的进程写编译产物
        }
    }

    // 4. 最小设备
    auto dev = bind_minimal_devices(new_root);
    if (!dev.ok) return dev;

    // 4b. 可写 /tmp（编译器 gcc/go/rustc/javac 需要临时目录）
    std::string tmp = new_root + "/tmp";
    mkdir(tmp.c_str(), 01777);
    mount("tmpfs", tmp.c_str(), "tmpfs", MS_NOSUID | MS_NODEV, "size=256m,mode=1777");

    // 5. pivot_root
    std::string old_root = new_root + "/.old_root";
    if (!mkdir_p(old_root)) return fail("mkdir old_root");
    if (syscall(SYS_pivot_root, new_root.c_str(), old_root.c_str()) != 0) {
        return fail("pivot_root");
    }
    if (chdir("/") != 0) return fail("chdir /");
    if (umount2("/.old_root", MNT_DETACH) != 0) return fail("umount old_root");
    rmdir("/.old_root");

    // 6. 新 PID ns 下挂私有 /proc
    mkdir("/proc", 0555);
    if (mount("proc", "/proc", "proc",
              MS_NOSUID | MS_NODEV | MS_NOEXEC, nullptr) != 0) {
        return fail("mount /proc");
    }
    return r;
}

SetupResult Manager::bind_minimal_devices(const std::string& new_root) {
    SetupResult r;
    std::string dev_dir = new_root + "/dev";
    mkdir(dev_dir.c_str(), 0755);
    if (mount("tmpfs", dev_dir.c_str(), "tmpfs",
              MS_NOSUID, "size=64k,mode=755") != 0) {
        r.ok = false;
        r.error = std::string("mount /dev tmpfs: ") + std::strerror(errno);
        return r;
    }

    struct DevNode { const char* name; mode_t mode; unsigned major, minor; };
    const DevNode nodes[] = {
        {"null",    S_IFCHR | 0666, 1, 3},
        {"zero",    S_IFCHR | 0666, 1, 5},
        {"full",    S_IFCHR | 0666, 1, 7},
        {"urandom", S_IFCHR | 0444, 1, 9},
        {"random",  S_IFCHR | 0444, 1, 8},
    };
    for (const auto& n : nodes) {
        std::string p = dev_dir + "/" + n.name;
        mknod(p.c_str(), n.mode, makedev(n.major, n.minor));  // best-effort
        chmod(p.c_str(), n.mode & 0777);                       // mknod obeys umask
    }
    // std{in,out,err}/fd → /proc/self/fd（best-effort，失败非致命）
    int rc = 0;
    rc = symlink("/proc/self/fd", (dev_dir + "/fd").c_str());
    (void)rc;  // 忽略错误：EEXIST 或权限问题不影响核心功能
    rc = symlink("/proc/self/fd/0", (dev_dir + "/stdin").c_str());
    (void)rc;
    rc = symlink("/proc/self/fd/1", (dev_dir + "/stdout").c_str());
    (void)rc;
    rc = symlink("/proc/self/fd/2", (dev_dir + "/stderr").c_str());
    (void)rc;
    return r;
}

pid_t Manager::clone_and_exec(int flags, const std::function<int()>& child_main) {
    char* stack = new (std::nothrow) char[kChildStackSize];
    if (stack == nullptr) return -1;

    // clone 无 CLONE_VM：子进程得到 COW 私有地址空间（含此栈的私有副本）。
    // child_main 引用父地址空间中的 std::function（COW 可读），仅在单线程 judge 下安全。
    pid_t child = clone(
        [](void* arg) -> int {
            auto* fn = static_cast<const std::function<int()>*>(arg);
            return (*fn)();
        },
        stack + kChildStackSize,
        flags | SIGCHLD,
        const_cast<std::function<int()>*>(&child_main));

    delete[] stack;  // 所有路径回收（子进程有自己的副本，删父副本安全；修 D16）
    return child;
}

bool Manager::drop_privileges() {
    if (setgroups(0, nullptr) != 0) return false;
    if (setresgid(kNobodyGid, kNobodyGid, kNobodyGid) != 0) return false;
    if (setresuid(kNobodyUid, kNobodyUid, kNobodyUid) != 0) return false;
    // 确认无法恢复为 root
    if (setuid(0) == 0) return false;
    return true;
}

} // namespace cppjudge::ns

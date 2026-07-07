#include "cppjudge/seccomp_manager.h"

#include <seccomp.h>

#include <cstdlib>

namespace cppjudge::seccomp {

namespace {

// 按 syscall 名（而非架构相关的 SYS_* 宏）维护白名单：
// 用 seccomp_syscall_resolve_name 在运行时解析，x86_64/aarch64 通用，
// 某 arch 不存在的名字自动跳过（修 D18 可移植性）。

// 运行时基础（C/C++ Strict）。始终含 execve/execveat。
// 未列出的（socket/connect/ptrace/mount/bpf/...）默认拒绝 → 天然阻断网络与逃逸。
std::vector<std::string> strict_names() {
    return {
        "read", "write", "readv", "writev", "pread64", "pwrite64",
        "open", "openat", "close", "lseek",
        "fstat", "newfstatat", "stat", "lstat", "statx",
        "fcntl", "ioctl",  // isatty(TCGETS)；stdio 为普通文件，TIOCSTI 无效（D2/D3 中和）
        "readlink", "readlinkat", "getdents64",
        "access", "faccessat", "faccessat2",
        "dup", "dup2", "dup3", "poll", "ppoll", "pselect6", "select",
        "mmap", "munmap", "mprotect", "brk", "mremap", "madvise",
        "execve", "execveat", "exit", "exit_group",
        "rt_sigaction", "rt_sigprocmask", "rt_sigreturn", "sigaltstack",
        "getpid", "gettid", "getuid", "geteuid", "getgid", "getegid",
        "clock_gettime", "clock_getres", "gettimeofday",
        "nanosleep", "clock_nanosleep", "getrandom",
        "futex", "sched_yield",
        "arch_prctl", "set_tid_address", "set_robust_list", "get_robust_list",
        "prlimit64", "getcwd", "uname", "sysinfo", "getrusage", "rseq",
    };
}

// Standard（Go/Rust）：多线程 + 调度 + epoll
std::vector<std::string> standard_names() {
    std::vector<std::string> v = strict_names();
    for (const char* n : {"clone", "clone3",
                          "sched_getaffinity", "sched_setaffinity",
                          "sched_getparam", "sched_getscheduler",
                          "tgkill", "tkill", "membarrier",
                          "epoll_create1", "epoll_ctl", "epoll_pwait",
                          "epoll_pwait2", "epoll_wait",
                          "eventfd2", "pipe2", "restart_syscall"}) {
        v.emplace_back(n);
    }
    return v;
}

// Extended（Python 等）
std::vector<std::string> extended_names() {
    std::vector<std::string> v = standard_names();
    for (const char* n : {"getxattr", "lgetxattr", "fgetxattr",
                          "sendfile", "copy_file_range",
                          "mkdirat", "unlinkat", "renameat2",
                          "socketpair",  // AF_UNIX 本地 IPC；socket() 仍禁 → 无网络
                          "readahead"}) {
        v.emplace_back(n);
    }
    return v;
}

// JVM（Java/Kotlin/Scala）
std::vector<std::string> jvm_names() {
    std::vector<std::string> v = extended_names();
    for (const char* n : {"shmget", "shmat", "shmdt", "shmctl",
                          "msync", "mincore", "times",
                          "sched_get_priority_max", "sched_get_priority_min",
                          "setpriority", "getpriority",
                          // JVM 运行时较广的系统调用面（据 strace 实测）
                          "statfs", "fstatfs", "sched_setattr", "sched_getattr",
                          "getcpu", "set_mempolicy", "get_mempolicy", "mbind",
                          "openat2", "name_to_handle_at", "clock_getres",
                          "rt_sigtimedwait", "rt_sigpending", "rt_sigsuspend",
                          "setrlimit", "getrlimit",
                          "fchdir", "flock", "ftruncate", "prctl",
                          // JVM 需要本地 socket；真正的网络隔离由 CLONE_NEWNET(空网络命名空间)
                          // 保证，故此处放行 socket/connect 不会带来网络访问。
                          "socket", "connect"}) {
        v.emplace_back(n);
    }
    return v;
}

// 编译白名单（修 D6）：Extended + 进程创建/回收 + 文件写，供 gcc/go/javac/rustc。
std::vector<std::string> compile_names() {
    std::vector<std::string> v = extended_names();
    for (const char* n : {"clone", "clone3", "fork", "vfork",
                          "wait4", "waitid", "pipe", "pipe2",
                          "ftruncate", "fchmod", "fchmodat", "umask",
                          "chdir", "fchdir", "symlinkat", "linkat",
                          "flock", "fsync", "fdatasync", "prctl",
                          "mkdir", "rmdir", "unlink", "rename"}) {
        v.emplace_back(n);
    }
    return v;
}

const std::vector<std::string>& run_names(SeccompProfile p) {
    static const std::vector<std::string> s = strict_names();
    static const std::vector<std::string> st = standard_names();
    static const std::vector<std::string> e = extended_names();
    static const std::vector<std::string> j = jvm_names();
    switch (p) {
        case SeccompProfile::Strict:   return s;
        case SeccompProfile::Standard: return st;
        case SeccompProfile::Extended: return e;
        case SeccompProfile::JVM:      return j;
    }
    return s;
}

} // namespace

SeccompProfile profile_for_lang(const std::string& lang) {
    if (lang == "c" || lang == "cpp" || lang == "c++") return SeccompProfile::Strict;
    if (lang == "go" || lang == "rust")                return SeccompProfile::Standard;
    if (lang == "java" || lang == "kotlin" || lang == "scala") return SeccompProfile::JVM;
    if (lang == "python3" || lang == "python" || lang == "node" ||
        lang == "ruby" || lang == "php" || lang == "perl")
        return SeccompProfile::Extended;
    return SeccompProfile::Strict;  // 未知 → 最安全
}

bool Manager::install(SeccompProfile profile, bool is_compile) {
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL_PROCESS);  // 默认拒绝
    if (ctx == nullptr) return false;

    static const std::vector<std::string> compile = compile_names();
    const std::vector<std::string>& names = is_compile ? compile : run_names(profile);

    for (const std::string& n : names) {
        int nr = seccomp_syscall_resolve_name(n.c_str());
        if (nr == __NR_SCMP_ERROR) continue;  // 本 arch 无此 syscall
        seccomp_rule_add(ctx, SCMP_ACT_ALLOW, nr, 0);
    }

    int rc = seccomp_load(ctx);
    seccomp_release(ctx);
    return rc == 0;
}

std::string Manager::violation_to_string(int syscall_num) {
    char* name = seccomp_syscall_resolve_num_arch(SCMP_ARCH_NATIVE, syscall_num);
    if (name != nullptr) {
        std::string result(name);
        std::free(name);
        return result;
    }
    return "unknown(" + std::to_string(syscall_num) + ")";
}

const std::vector<std::string>& Manager::allowlist_for_testing(SeccompProfile p) {
    return run_names(p);
}

} // namespace cppjudge::seccomp

# mikuOJ 架构说明

## 总览

mikuOJ 是一条单机多语言判题流水线：

```text
加载题目
  -> 检测语言
  -> 沙箱内编译或解释执行准备
  -> 沙箱内运行每个测试点
  -> 比较输出
  -> 聚合判决
  -> 写入日志
```

核心模块：

```text
CLI(main)
  -> ProblemManager
  -> LanguageManager
  -> Compiler
  -> SandboxBackend
       -> LinuxNsSandbox
            -> Namespace Manager
            -> Cgroup Manager
            -> Seccomp Manager
  -> Comparator
  -> Logger
```

正式 OJ 后端只保留 Linux 安全沙箱：

```text
LinuxNsSandbox
  = namespace + cgroup v2 + seccomp + privilege drop
```

## 沙箱抽象

`include/cppjudge/sandbox.h` 暴露：

- `SandboxRequest`：一次沙箱执行请求。
- `SandboxResult`：一次沙箱执行结果。
- `SandboxBackend::execute(...)`：执行入口。
- `SandboxBackend::name()`：后端名称。
- `make_sandbox(...)`：根据 `auto`、`linux-ns`、`nsjail` 创建安全后端。

合法后端名：

```text
auto
linux-ns
nsjail
```

## Linux 安全后端生命周期

顺序必须保持：

```text
父进程:
  create/apply cgroup
  -> clone child with namespaces
  -> attach child to cgroup
  -> release child
  -> monitor wall time / CPU / output / memory
  -> collect stats
  -> derive verdict
  -> cleanup

子进程:
  open stdio on host paths
  -> dup2
  -> setup rootfs and bind mounts
  -> chdir(/box)
  -> setrlimit
  -> wait for parent cgroup attach
  -> drop privileges
  -> install seccomp
  -> execve
```

关键约束：

- cgroup attach 必须在 execve 前完成。
- seccomp 必须是 execve 前最后一步。
- 所有 setup syscall 必须在 seccomp 前完成。
- 判题系统自身错误返回 SE，不能混入用户 RE。

## 隔离层

| 层级 | 机制 | 作用 |
| ---- | ---- | ---- |
| 文件系统 | mount namespace + tmpfs root + bind 白名单 + pivot_root | 只暴露白名单路径 |
| 进程/网络 | pid/net/ipc/uts namespace | 隔离 PID、网络和 IPC |
| 资源 | cgroup v2 + rlimit | 限制内存、进程数、CPU、输出大小 |
| 系统调用 | seccomp 默认拒绝 + 白名单 | 阻断危险 syscall，SIGSYS 映射为 SV |
| 权限 | setgroups/setresgid/setresuid 到 nobody | 避免以 root 执行用户程序 |

## 判决优先级

```text
SE > CE > TLE > MLE > OLE > SV > RE > WA > AC
```

`derive_verdict(...)` 根据退出码、信号、资源统计、超限状态和输出大小推导单测点运行结果；`WA` 由 Comparator 在程序正常运行后决定。

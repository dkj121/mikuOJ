# cppjudge 架构

## 总览

cppjudge 是一条判题流水线：**加载题目 → 检测语言 → 编译（沙箱内）→ 逐测试点运行（沙箱内）→ 比较输出 → 聚合判决 → 写日志**。核心是一个**跨平台沙箱抽象**，把"平台无关的判题逻辑"与"平台相关的隔离机制"分开。

```
   ┌──── 平台无关（macOS + Linux 均编译/测试）────┐
CLI(main) ─► ProblemManager ─► LanguageManager ─► Compiler ─► Comparator ─► Logger
                    │                                    │
                    └──────────► Sandbox 抽象 ◄──────────┘
   ┌── SandboxBackend 接口 + make_sandbox 工厂 + sandbox_common(共享编排) ──┐
   │  LinuxNsSandbox (#ifdef __linux__, 安全)     BuiltinSandbox (不安全)    │
   │  ns + cgroup + seccomp + privdrop            fork + setrlimit + getrusage│
   │  sandbox_linux.cpp                            sandbox_builtin.cpp        │
   │  + ns_manager / cgroup_manager / seccomp_manager                        │
   └────────────────────────────────────────────────────────────────────────┘
```

## 跨平台策略

- **平台无关模块**（一份实现，全平台编译）：`common.h`、`problem`、`language`、`compiler`、`comparator`、`logger`、`log`(spdlog/cpptrace)、CLI。
- **沙箱抽象** `sandbox.h`：`SandboxRequest` / `SandboxResult` / 抽象 `SandboxBackend{ execute(); is_secure(); name(); }` / `make_sandbox(type)`。
- **共享编排** `sandbox_common.cpp`：判决推导 `derive_verdict`、最小环境、argv/envp 构造——两后端复用，不重复。
- **平台特有**（宏 + CMake 双隔离）：Linux 编译 `sandbox_linux.cpp` + `ns/cgroup/seccomp_manager`；macOS 只编译便携 `sandbox_builtin.cpp`。CMake 用 `if(APPLE)/elseif(UNIX)` 门控，libseccomp 仅 Linux 依赖。
- **工具链解析**：`LanguageManager` 运行时在 PATH 与常见目录解析编译器/解释器，避免硬编码 `/usr/bin/g++`（macOS 路径不同）。

## 安全后端（linux-ns）子进程生命周期

顺序**绝对不可乱**（父子通过管道同步）：

```
父：create+apply cgroup → clone(NEWNS|NEWPID|NEWNET|NEWIPC|NEWUTS)
                        → attach child 到 cgroup → 放行(proceed)
                        → 墙上/CPU/输出 监控 → 回收 → 采集统计 → 判决
子：open stdio(宿主路径，pivot 前) → dup2
   → setup_rootfs(tmpfs 根 + bind 白名单 + /dev + /tmp + pivot_root)
   → chdir(/box) → setrlimit(stack/fsize/cpu)
   → [ready → 等父 attach cgroup → proceed 门控]     ← 保证限制先于 execve 生效
   → drop_privileges(nobody)
   → seccomp(运行阶段严格白名单；编译阶段跳过)
   → execve
```

**关键不变量**：
- stdio 在 pivot 前打开（fd 存活），失败绝不回退继承 fd（防判决伪造）。
- cgroup attach 在 execve 前完成（限制对整棵子进程树生效）。
- seccomp 是 execve 前最后一步；所有 setup syscall 在其之前。
- 网络隔离由空 net namespace 保证（seccomp 网络限制为纵深防御）。

## 隔离层

| 层 | 机制 | 作用 |
|----|------|------|
| 文件系统 | mount ns + tmpfs 最小根 + 只读 bind(`nosuid/nodev/noexec`) + pivot_root | 只暴露白名单路径；写仅限 `/box`、`/tmp` |
| 进程/网络 | pid/net/ipc/uts ns | 独立 PID 空间、无网络、隔离 IPC |
| 资源 | cgroup v2（memory/pids/cpu）+ rlimit（stack/fsize/cpu） | 内存/进程/CPU/输出上限 |
| 系统调用 | seccomp 默认拒绝 + 分级白名单 | 阻断危险 syscall；违规 SIGSYS→SV |
| 权限 | setgroups/setresgid/setresuid → nobody | 非 root 运行 |

## seccomp 分级

| Profile | 语言 | 说明 |
|---------|------|------|
| Strict | C/C++ | 最小运行时 syscall |
| Standard | Go/Rust | + 多线程/调度/epoll |
| Extended | Python3 等 | + 文件/管道/元数据 |
| JVM | Java | + JVM 所需（含本地 socket；网络仍由 net ns 阻断） |

按 syscall **名**运行时解析（`seccomp_syscall_resolve_name`），x86_64/aarch64 通用。编译阶段不启用 seccomp（编译器可信，由其他层隔离）。

## 判决优先级（单测试点）

`SE > CE > TLE > MLE > OLE > SV > RE > WA > AC`。多测试点取最高优先级；`derive_verdict` 依据 (退出码, 信号, 资源, 限制, 输出) 推导。

## 平台差异

| 场景 | macOS builtin | Linux linux-ns |
|------|---------------|----------------|
| 隔离 | 无（仅 rlimit 计量） | 完整 |
| 内存限制 | 尽力（RSS 计量） | cgroup memory.max |
| 峰值 RSS | `getrusage`(bytes) | cgroup memory.peak |
| SV/安全测试 | 不适用 | 适用 |
| 用途 | 开发/流水线自测 | 生产判题 |

## 参考

- 设计与实现手册：[soc施工手册(新版).md](soc施工手册(新版).md)
- 相对手册参考实现的安全/正确性修复清单见 [CHANGELOG](../CHANGELOG.md) 与提交历史。

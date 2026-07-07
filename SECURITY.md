# 安全策略

mikuOJ 的核心职责是**安全地运行不受信任的代码**，因此隔离缺陷（沙箱逃逸、资源限制绕过、信息泄漏）属于高优先级安全问题。

## 支持的版本

项目处于早期开发阶段（0.x）。安全修复仅针对 `main` 分支的最新提交。

## 报告漏洞

**请勿在公开 issue 中披露沙箱逃逸或隔离绕过。**

- 通过 GitHub 的 **Security Advisories**（仓库 → Security → Report a vulnerability）私下报告，或联系维护者（NJUPT-SAST）。
- 请附：受影响的后端（`linux-ns` / `builtin`）、内核版本、复现的提交代码（PoC）、观察到的行为与预期行为。
- 我们会尽快确认并在修复后致谢报告者。

## 威胁模型（简述）

- **可信**：判题系统本体、编译器/解释器工具链、题目数据。
- **不可信**：提交的源代码及其编译产物、其运行时行为、其输出。
- **安全后端（`linux-ns`）的保证**：
  - 无网络（空 network namespace）
  - 无法访问白名单之外的文件系统（最小 tmpfs 根 + 只读 bind + `nosuid/nodev/noexec`）
  - 以 `nobody` 运行（降权，非 root）
  - 内存/进程/输出受 cgroup 与 rlimit 限制
  - 运行阶段 seccomp 默认拒绝，仅放行白名单 syscall（违规 SIGSYS → SV）
- **已知限制**：
  - `builtin` 后端**不提供隔离**，仅用于开发；生产模式（`CPPJUDGE_ENV=production`）会拒绝它。
  - 编译阶段不启用 seccomp（编译器可信，由 namespace/cgroup/降权/最小文件系统隔离）。
  - macOS 构建无隔离机制，切勿用于判不受信任的提交。
  - 未使用 user namespace（降权到 `nobody` 而非 uid 映射）——后续硬化方向。

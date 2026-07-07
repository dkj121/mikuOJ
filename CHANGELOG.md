# Changelog

本项目遵循 [Keep a Changelog](https://keepachangelog.com/zh-CN/1.1.0/) 与 [语义化版本](https://semver.org/lang/zh-CN/)。

## [Unreleased]

### Added
- 跨平台沙箱抽象 `SandboxBackend` + 工厂 + 共享编排（判决推导、环境构造）。
- **Linux 安全后端** `linux-ns`：mount/pid/net/ipc/uts namespace + cgroup v2 + seccomp（默认拒绝白名单）+ 降权到 `nobody`。
- **macOS/便携 builtin 后端**：`fork`+`setrlimit`+`getrusage`（不隔离，仅开发）。
- 判题流水线模块：Problem（yaml-cpp 解析）、Language（6 语言、运行时工具链解析）、Compiler（沙箱内编译）、Comparator（exact/floating，locale 无关）、Logger（转义 JSON）、Doctor、CLI。
- 诊断日志基于 **spdlog**（stderr），异常调用栈基于 **cpptrace**。
- 6 语言（C++/C/Python3/Java/Go/Rust）示例提交 + 9 判决边界用例。
- 单元测试（跨平台）、集成测试（9 判决 + 多语言 + 浮点）、安全测试（5 攻击用例）。
- 开源规范文件：LICENSE、CONTRIBUTING、CODE_OF_CONDUCT、SECURITY、issue/PR 模板、`.clang-format`/`.editorconfig`/`.gitattributes`。

### Fixed（相对施工手册参考实现的安全/正确性修复，D1–D19）
- 执行沙箱现会挂载工作目录并在 `pivot_root` 前打开 stdio，杜绝"找不到程序/输出丢失/继承 fd 伪造判决"（D2）。
- 不受信任代码降权到 `nobody` 运行，不再以 root（D3）。
- seccomp 违规（SIGSYS）正确映射为 `SV`（D4）。
- cgroup 控制器委派 + 容器嵌套回退，限制真正生效；attach-before-exec 门控（D1）。
- 编译与运行使用不同隔离策略，编译器不再被 seccomp 误杀（D6）。
- CPU 采样实现 CPU-TLE；OLE 由输出监控可靠触发（D7、D8）。
- `problem.json` 限制不再被默认值覆盖（D10）；`sandbox_type` 真正分发 + 生产 fail-closed（D11）；setup 失败判为 SE（D12）。
- Logger JSON 转义、cgroup 安全解析与竞态销毁、mount 硬化、多线程运行时 pids 默认值等（D13–D19）。

[Unreleased]: https://github.com/orange11-forever/Temp/commits/main

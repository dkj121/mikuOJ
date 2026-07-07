# Changelog

本项目遵循 Keep a Changelog 和语义化版本。

## [Unreleased]

### Added

- 跨模块判题流水线：Problem、Language、Compiler、Sandbox、Comparator、Logger、Doctor、CLI。
- Linux 安全后端 `linux-ns`：namespace、cgroup v2、seccomp 默认拒绝白名单、降权到 `nobody`。
- 六种语言示例：C++、C、Python3、Java、Go、Rust。
- 单元测试、集成测试和安全测试目录。

### Changed

- 正式 OJ 路径只保留 Linux `linux-ns` 安全后端。
- 沙箱工厂和 CMake 构建目标收敛到 Linux 安全后端。
- `--sandbox-type` 合法值收敛为 `auto`、`linux-ns`、`nsjail`。

### Fixed

- 判题系统 setup / exec 失败归类为 SE。
- seccomp SIGSYS 映射为 SV。
- cgroup attach-before-exec 保证资源限制先于用户程序生效。

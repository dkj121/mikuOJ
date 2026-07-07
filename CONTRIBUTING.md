# 贡献指南

感谢参与 mikuOJ！本文档说明开发流程与规范。

## 开发环境

- **判题功能开发/测试**需要 Linux（WSL2 或裸机），内核 5.15+、cgroup v2、root。
- **平台无关模块**（comparator / problem / language / logger 等）在 macOS 也可完整构建与单测。
- 依赖见 [README 前置条件](README.md#前置条件)。

## 构建与测试

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug && cmake --build build -j
ctest --test-dir build -L unit --output-on-failure          # 跨平台
sudo ctest --test-dir build -L integration --output-on-failure  # Linux
sudo ctest --test-dir build -L security --output-on-failure     # Linux
```

## Pre-commit 钩子

克隆后执行一次：

```bash
pip install pre-commit
pre-commit install
```

之后每次 `git commit` 自动运行 clang-format、行尾空白清理、大文件检查。配置见 `.pre-commit-config.yaml`、代码风格见 `.clang-format`（Google 基线、4 空格缩进、100 列）。

## 提交规范（Conventional Commits）

格式：`<type>(<scope>): <description>`

| type | 用途 |
|------|------|
| `feat` | 新模块或功能 |
| `fix` | 缺陷修复 |
| `test` | 测试 |
| `build` | CMake、依赖 |
| `docs` | 文档 |
| `refactor` | 重构 |
| `chore` | 脚本、配置 |

| scope | 模块 |
|-------|------|
| `sandbox` | 沙箱后端与抽象 |
| `ns` / `cgroup` / `seccomp` | Linux 隔离管理器 |
| `problem` / `language` / `compiler` / `comparator` / `logger` | 判题流水线模块 |
| `cli` / `doctor` | 前端与诊断 |
| `logging` | spdlog/cpptrace 诊断日志 |

示例：`feat(sandbox): Linux ns+cgroup+seccomp secure backend`

## 分支与 PR

- 从 `main` 切功能分支：`feat/<topic>`、`fix/<topic>`。
- 不直接推 `main`；通过 Pull Request 合并。
- PR 需通过 CI（构建 + 单元测试）。涉及沙箱/隔离的改动请在 PR 描述中说明如何在 Linux 上验证。
- 每个提交尽量自包含（模块 + 其单测 + CMake 注册），保持 CI 常绿。

## 模块任务分工（施工手册）

| 模块 | 负责人 |
|------|--------|
| 脚手架 + 共享类型、Namespace Manager、Comparator | @褚浩然 |
| Cgroup Manager | @裴睿泽 |
| Seccomp Manager、Logger | @杨伟杰 |
| Sandbox Core、Compiler | @席源 |
| Problem Manager、CLI + Doctor | @王毅 |
| Language Manager、集成/安全测试 | @严浩容 |

> 注：本实现在原施工手册基础上做了跨平台重构与大量安全修复，详见提交历史与 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。

## 安全

沙箱逃逸/隔离相关问题请勿公开 issue，按 [SECURITY.md](SECURITY.md) 私下报告。

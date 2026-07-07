# cppjudge

> 单机、多语言、安全隔离的竞赛判题系统 · A single-machine, multi-language, sandboxed competitive-programming judge

[![CI](https://github.com/orange11-forever/Temp/actions/workflows/ci.yml/badge.svg)](https://github.com/orange11-forever/Temp/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C.svg)](https://en.cppreference.com/w/cpp/20)

cppjudge 在 **Linux（WSL2 / 裸机）** 上用 **namespace + cgroup v2 + seccomp + 权限丢弃** 四层隔离，安全地编译并运行不受信任的提交，比较输出并聚合判决。同时提供一个 **macOS 开发后端**（无隔离，仅供本机开发与流水线自测）。

## 特性

- **6 种语言**：C++ / C / Python3 / Java / Go / Rust（自动按扩展名检测，或 `--lang` 指定）
- **真隔离**（Linux `linux-ns` 后端）：mount namespace + pivot_root 最小根、cgroup v2 内存/CPU/进程限制、seccomp 默认拒绝白名单、降权到 `nobody`、空网络命名空间
- **9 种判决**：见下表
- **双比较模式**：`exact`（逐行、空白规范化）与 `floating`（逐 token、绝对/相对误差）
- **跨平台**：同一套判题流水线在 macOS 与 Linux 均可构建；平台差异用宏隔离，公共逻辑只写一份
- **结构化输出**：stdout 输出 JSON 判决，`build/judge_log.json` 记录详细结果；诊断日志（spdlog）走 stderr，异常带调用栈（cpptrace）

### 判决（Verdict）

| 缩写 | 含义 | 触发 |
|------|------|------|
| AC  | Accepted | 全部测试点通过 |
| WA  | Wrong Answer | 正常退出但输出不符 |
| TLE | Time Limit Exceeded | CPU 或墙上时间超限 |
| MLE | Memory Limit Exceeded | 内存超限（cgroup OOM） |
| OLE | Output Limit Exceeded | 输出大小超限 |
| RE  | Runtime Error | 非零退出或信号终止 |
| SV  | Syscall Violation | seccomp 拦截（SIGSYS） |
| CE  | Compile Error | 编译失败 |
| SE  | System Error | 判题系统自身故障（退出码 3） |

## 架构

```
   ┌──── 平台无关（macOS + Linux 均编译/测试）────┐
CLI ─► Problem ─► Language ─► Compiler ─► Comparator ─► Logger
                    │                          │
                    └──────► Sandbox 抽象 ◄─────┘
   ┌── SandboxBackend 接口 + make_sandbox 工厂 + 共享编排 ──┐
   │  LinuxNsSandbox (安全)          BuiltinSandbox (不安全) │
   │  ns+cgroup+seccomp+privdrop     fork+setrlimit+getrusage│
   └───────────────────────────────────────────────────────┘
```

详见 [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)。设计与实现手册见 [docs/soc施工手册(新版).md](docs/soc施工手册(新版).md)。

## 前置条件

**Linux（生产/安全判题）**：内核 5.15+（推荐 6.x）、cgroup v2、root 权限
```bash
sudo apt install build-essential cmake pkg-config git zlib1g-dev \
  libseccomp-dev libyaml-cpp-dev libspdlog-dev libfmt-dev libgtest-dev \
  python3 default-jdk golang-go rustc
# cpptrace 由 CMake FetchContent 自动获取
```

**macOS（仅开发/自测，无隔离）**：
```bash
brew install cmake yaml-cpp googletest spdlog cpptrace
```

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 快速开始

```bash
# 环境检查
sudo ./build/cppjudge doctor

# 判题（自动检测语言）。Linux 默认用安全后端 linux-ns。
sudo ./build/cppjudge judge --problem problems/A+B --submission submissions/cpp/solution.cpp

# 指定语言 / 限制 / 后端
sudo ./build/cppjudge judge --problem problems/A+B \
  --submission submissions/python3/solution.py \
  --time-limit-ms 1000 --memory-limit-mb 128 --compare-mode exact

# macOS 开发自测（不安全 builtin 后端）
./build/cppjudge judge --problem problems/A+B --submission submissions/cpp/solution.cpp --sandbox-type builtin

# 查看详细结果
cat build/judge_log.json
```

> ⚠️ **安全**：只有 Linux `linux-ns` 后端提供真正隔离。`builtin` 后端（及 macOS 构建）**不隔离**，`CPPJUDGE_ENV=production` 时会被拒绝运行（fail-closed）。绝不要用 builtin 判不受信任的提交。

## 测试

```bash
# 单元测试（跨平台，无需 root）
ctest --test-dir build -L unit --output-on-failure

# 集成测试（9 判决 + 多语言，Linux + root）
sudo ctest --test-dir build -L integration --output-on-failure

# 安全测试（沙箱隔离，Linux + root）
sudo ctest --test-dir build -L security --output-on-failure

# 一键回归
./scripts/run_tests.sh
```

## 项目结构

```
include/cppjudge/   共享类型与各模块接口
src/                模块实现（平台无关 + Linux/builtin 后端）
problems/A+B/       示例题目
submissions/        6 语言示例提交 + 判决边界用例
tests/{unit,integration,security}/  测试
scripts/            回归脚本
docs/               架构文档 + 施工手册
```

## 贡献

见 [CONTRIBUTING.md](CONTRIBUTING.md)（Conventional Commits、分支规范、pre-commit、任务分工）。安全问题请按 [SECURITY.md](SECURITY.md) 报告。参与者需遵守 [行为准则](CODE_OF_CONDUCT.md)。

## 许可

[MIT](LICENSE) © 2026 NJUPT-SAST

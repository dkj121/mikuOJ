# mikuOJ

单机、多语言、安全隔离的竞赛判题系统。

mikuOJ 面向 WSL2 / Linux 部署，正式 OJ 路径只使用 `linux-ns` 安全沙箱后端：namespace、cgroup v2、seccomp 和权限丢弃共同完成隔离。

## 特性

- 支持 C++、C、Python3、Java、Go、Rust 六种语言。
- 编译型语言在沙箱内编译；Python3 跳过编译阶段，直接解释执行。
- Linux 安全沙箱：mount / pid / net / ipc / uts namespace、pivot_root 最小根、cgroup v2 资源限制、seccomp 默认拒绝白名单、降权到 `nobody`。
- 支持 `AC / WA / TLE / MLE / OLE / RE / SV / CE / SE` 判决。
- 输出比较支持 exact 和 floating 两种模式。
- stdout 只输出最终 JSON 结果；诊断日志输出到 stderr；详细日志写入 `build/judge_log.json`。

## 架构概览

```text
CLI
  -> Problem Manager
  -> Language Manager
  -> Compiler
  -> Sandbox Core
       -> LinuxNsSandbox
            -> Namespace Manager
            -> Cgroup Manager
            -> Seccomp Manager
  -> Comparator
  -> Logger
```

正式后端：

```text
linux-ns / nsjail alias
  = namespace + cgroup v2 + seccomp + privilege drop
```

## 前置条件

Linux / WSL2：

```bash
sudo apt install build-essential cmake pkg-config git zlib1g-dev \
  libseccomp-dev libyaml-cpp-dev libspdlog-dev libfmt-dev libgtest-dev \
  python3 default-jdk golang-go rustc
```

## 构建

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## 快速开始

```bash
sudo ./build/cppjudge doctor

sudo ./build/cppjudge judge \
  --problem problems/A+B \
  --submission submissions/cpp/solution.cpp

sudo ./build/cppjudge judge \
  --problem problems/A+B \
  --submission submissions/python3/solution.py \
  --time-limit-ms 1000 \
  --memory-limit-mb 128 \
  --compare-mode exact

cat build/judge_log.json
```

可选沙箱参数：

```text
--sandbox-type auto
--sandbox-type linux-ns
--sandbox-type nsjail
```

`auto` 在 Linux 上映射到安全后端 `linux-ns`。

## 测试

```bash
ctest --test-dir build -L unit --output-on-failure
sudo ctest --test-dir build -L integration --output-on-failure
sudo ctest --test-dir build -L security --output-on-failure

./scripts/run_tests.sh
```

## 项目结构

```text
include/cppjudge/   共享类型与各模块接口
src/                模块实现和 Linux 安全沙箱后端
problems/A+B/       示例题目
submissions/        多语言示例提交和边界用例
tests/              单元、集成、安全测试
scripts/            回归脚本
docs/               架构文档和施工手册
```

## 安全说明

只有 Linux `linux-ns` 后端属于正式 OJ 沙箱。

## 许可证

[MIT](LICENSE) © 2026 NJUPT-SAST

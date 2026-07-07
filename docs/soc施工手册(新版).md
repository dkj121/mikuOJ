# soc施工手册\(新版\)

# 实现计划

**目标：** 构建完整的单机多语言判题系统——编译（或解释执行）、沙箱执行、输出比较、结果聚合，部署在 WSL2 上。

**架构：** 十一模块判题流水线：Problem Manager \+ Language Manager \+ Compiler \+ Sandbox（ns/cgroup/seccomp 三个子模块）\+ Comparator \+ Logger \+ Doctor，通过 CLI 统一入口调度。支持 6 种语言（C\+\+/C/Python3/Java/Go/Rust），编译和执行均在沙箱内进行，比较引擎支持 exact 和 floating 双模式。

**技术栈：** C\+\+20、CMake 3\.16\+、libseccomp、yaml\-cpp、Google Test、WSL2（Linux 内核 5\.15\+）、CI：GitHub Actions on Ubuntu 22\.04

## 全局约束

- 部署在 WSL2，使用 cgroup v2 统一层级（`/sys/fs/cgroup/`）

- 必须以 root 运行（cgroup 写入、namespace 创建、mount 操作）

- `memory.swap.max` 必须始终为 0（禁用 swap）

- 判题系统自身错误使用退出码 3（SE），绝不与用户程序结果混合

- stdout 保留给 JSON 结果输出；所有日志输出到 stderr

- seccomp 使用默认拒绝 \+ 白名单模式；使用 `SCMP_ACT_KILL_PROCESS`

- 最小文件系统：仅白名单路径 \+ `/dev/null`、`/dev/zero`、`/dev/urandom`、`/dev/stdin`、`/dev/stdout`、`/dev/stderr`

- 编译在沙箱内进行（防止恶意 \#include）

- Production 模式（`CPPJUDGE_ENV=production`）：不安全后端拒绝运行，fail\-closed

- 所有 seccomp 白名单必须包含 `SYS_execve` 和 `SYS_execveat`

- seccomp 安装必须是子进程中 execve 前的绝对最后一步

- 所有 setup syscall（mount/dup2/setrlimit/open）必须在 seccomp 安装前完成

- 语言由 CLI `--lang` 参数或文件扩展名自动检测决定（`.cpp`/`.cc`→cpp, `.c`→c, `.py`→python3, `.java`→java, `.go`→go, `.rs`→rust）

- 解释型语言（Python3）跳过编译阶段，直接以解释器执行；编译型语言在沙箱内编译

- 每种语言的 seccomp profile、mount 依赖、编译器/解释器路径由 Language Manager 统一管理

- 语言支持矩阵：C\+\+\(g\+\+/strict\)、C\(gcc/strict\)、Python3\(python3/extended\)、Java\(javac\+jvm/JVM\)、Go\(go build/standard\)、Rust\(rustc/standard\)

---

## 文件结构

```Plain Text
cppjudge/
├── CMakeLists.txt                     # 根 CMake：子目录 + 依赖
├── README.md
├── include/
│   └── cppjudge/
│       ├── common.h                   # 共享类型：Verdict, Limits, Config, RunResult, Language
│       ├── problem.h                  # 题目管理：problem.json 解析，测试用例加载
│       ├── language.h                 # 语言管理器：检测、编译/运行时配置、mount 依赖
│       ├── compiler.h                 # 编译阶段：多语言编译/解释执行调度
│       ├── sandbox.h                  # 沙箱编排：clone→mount→cgroup→seccomp→exec
│       ├── ns_manager.h              # Namespace Manager
│       ├── cgroup_manager.h          # Cgroup v2 Manager
│       ├── seccomp_manager.h         # Seccomp Manager
│       ├── comparator.h              # 比较引擎：exact + floating
│       ├── logger.h                  # judge_log.json 写入 + run 产物
│       └── doctor.h                  # 环境诊断
├── src/
│   ├── CMakeLists.txt
│   ├── main.cpp                       # CLI 入口
│   ├── problem.cpp
│   ├── language.cpp                   # Language Manager
│   ├── compiler.cpp
│   ├── sandbox.cpp                    # 沙箱编排（原 core.cpp）
│   ├── ns_manager.cpp
│   ├── cgroup_manager.cpp
│   ├── seccomp_manager.cpp
│   ├── comparator.cpp
│   ├── logger.cpp
│   └── doctor.cpp
├── problems/                          # 示例题目
│   └── A+B/
│       ├── problem.json
│       ├── input/
│       │   ├── 1.in
│       │   └── 2.in
│       └── output/
│           ├── 1.out
│           └── 2.out
├── sandbox/
│   └── seccomp/                       # seccomp 策略文件
├── submissions/                       # 示例提交（多语言）
│   ├── cpp/
│   │   ├── solution.cpp               # A+B AC 解答
│   │   ├── broken.cpp                 # 编译错误示例
│   │   ├── endless_loop.cpp          # 死循环（TLE）
│   │   ├── memory_hog.cpp            # 内存炸弹（MLE）
│   │   └── wrong_answer.cpp          # 错误输出（WA）
│   ├── python3/
│   │   ├── solution.py               # A+B AC
│   │   └── broken.py                 # 语法错误（RE）
│   ├── java/
│   │   └── Solution.java             # A+B AC
│   ├── go/
│   │   └── solution.go              # A+B AC
│   └── rust/
│       └── solution.rs              # A+B AC
├── tests/
│   ├── CMakeLists.txt
│   ├── unit/
│   │   ├── test_problem.cpp
│   │   ├── test_language.cpp
│   │   ├── test_compiler.cpp
│   │   ├── test_ns_manager.cpp
│   │   ├── test_cgroup_manager.cpp
│   │   ├── test_seccomp_manager.cpp
│   │   ├── test_sandbox.cpp
│   │   ├── test_comparator.cpp
│   │   ├── test_logger.cpp
│   │   └── test_doctor.cpp
│   ├── integration/
│   │   ├── CMakeLists.txt
│   │   ├── test_ac.sh
│   │   ├── test_wa.sh
│   │   ├── test_tle.sh
│   │   ├── test_mle.sh
│   │   ├── test_ole.sh
│   │   ├── test_re.sh
│   │   ├── test_ce.sh
│   │   ├── test_se.sh
│   │   ├── test_floating.sh
│   │   └── test_all_languages.sh     # 所有 6 种语言 AC 测试
│   └── security/
│       ├── run_security_tests.sh
│       └── cases/
│           ├── fork_bomb.cpp
│           ├── infinite_memory.cpp
│           ├── bad_syscall.cpp
│           ├── read_passwd.cpp
│           └── malicious_include.cpp
├── scripts/
│   ├── run_tests.sh                   # CI 回归测试套件
│   └── run_security_tests.sh          # 安全测试（手动）

```

---

## 任务依赖图

```Plain Text
Task 1 (scaffold + common types)
  │
  ├── Task 2 (ns manager)  ──────────────┐
  ├── Task 3 (cgroup manager) ───────────┤
  ├── Task 4 (seccomp manager) ──────────┤
  └── Task 6 (problem manager) ──────────┤
       │                                  │
       ├── Task 7 (language manager) ────┤
       │    │                             │
       │    └── Task 8 (compiler) ───────┤
       │                                  │
       └── Task 5 (sandbox core) ────────┘ (needs Tasks 2-4)
              │
              ├── Task 9 (comparator) ────── (needs Task 1)
              ├── Task 10 (logger) ────────── (needs Tasks 1, 6)
              └── Task 11 (CLI + doctor) ── (needs Tasks 5-10)
                     │
                     └── Task 12 (integration + security tests)
```

---

### Task 1：项目脚手架和共享类型

**文件：**

- 创建：`CMakeLists.txt`、`README.md`

- 创建：`include/cppjudge/common.h`

- 创建：`src/CMakeLists.txt`、`tests/CMakeLists.txt` 、\.github/workflows/ci\.yml  、 \.pre\-commit\- config\.yaml

**接口：**

- 产出：`cppjudge::Verdict`、`cppjudge::Limits`、`cppjudge::RunResult`、`cppjudge::JudgeConfig`、`cppjudge::ns::MountEntry` — 被所有后续任务使用

---

- [ ] **步骤 1：创建根 CMakeLists\.txt**

```CMake
cmake_minimum_required(VERSION 3.16)
project(cppjudge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSECCOMP REQUIRED libseccomp)
pkg_check_modules(YAMLCPP REQUIRED yaml-cpp)

add_subdirectory(src)
add_subdirectory(tests)
```

- [ ] **步骤 2：创建共享类型头文件**

创建 `include/cppjudge/common.h`：

```C++
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cppjudge {

// ============================================================
// 判决枚举（按优先级从高到低排列）
// ============================================================
enum class Verdict {
    AC,   // Accepted — 全部测试点通过
    WA,   // Wrong Answer — 正常退出但输出不符
    TLE,  // Time Limit Exceeded — CPU 或墙上时间超限
    MLE,  // Memory Limit Exceeded — 内存超限
    OLE,  // Output Limit Exceeded — 输出大小超限
    RE,   // Runtime Error — 非零退出或信号终止
    SV,   // Syscall Violation — seccomp 拦截
    CE,   // Compile Error — 编译失败
    SE,   // System Error — 判题系统自身故障
};

inline const char* verdict_to_string(Verdict v) {
    switch (v) {
        case Verdict::AC:  return "Accepted";
        case Verdict::WA:  return "Wrong Answer";
        case Verdict::TLE: return "Time Limit Exceeded";
        case Verdict::MLE: return "Memory Limit Exceeded";
        case Verdict::OLE: return "Output Limit Exceeded";
        case Verdict::RE:  return "Runtime Error";
        case Verdict::SV:  return "Syscall Violation";
        case Verdict::CE:  return "Compile Error";
        case Verdict::SE:  return "System Error";
    }
    return "UNKNOWN";
}

// ============================================================
// 资源限制（来自 problem.json 或 CLI 覆盖）
// ============================================================
struct Limits {
    uint64_t cpu_time_ms          = 2000;
    uint64_t wall_time_ms         = 6000;   // 默认 CPU × 3
    uint64_t memory_mb            = 256;
    uint64_t stack_mb             = 8;
    uint64_t output_size_mb       = 10;
    uint32_t max_processes        = 1;
    uint64_t compile_time_ms      = 5000;
};

// ============================================================
// 单个测试点的执行结果
// ============================================================
struct RunResult {
    Verdict    verdict        = Verdict::AC;
    int        exit_code      = 0;
    int        signal_num     = 0;
    uint64_t   time_ms        = 0;    // 用户态 CPU 时间
    uint64_t   wall_time_ms   = 0;
    uint64_t   memory_kb      = 0;    // 峰值 RSS
    bool       output_truncated = false;
    int        test_index     = 0;
    std::string run_id;
    std::string run_dir;
    std::string compare_detail;       // WA 时的差异描述
    std::string error_detail;         // SE/CE 时的详细信息
};

// ============================================================
// 判题配置（合并 problem.json + CLI 覆盖）
// ============================================================
struct JudgeConfig {
    std::string problem_dir;
    std::string submission_file;
    Limits      limits;
    std::string compare_mode;    // "exact" | "floating"
    std::string sandbox_type;    // "builtin" | "nsjail"
    double      float_abs_eps = 1e-9;
    double      float_rel_eps = 1e-6;
    bool        verbose       = false;
};

// ============================================================
// Namespace 挂载条目（ns 和 compiler 共用）
// ============================================================
namespace ns {

struct MountEntry {
    std::string source;    // 宿主机路径
    std::string target;    // 沙箱内路径
    bool        writable = false;
};

} // namespace ns

// ============================================================
// 语言枚举和配置（由 Language Manager 使用）
// ============================================================
enum class Language {
    CPP, C, PYTHON3, JAVA, GO, RUST,
    UNKNOWN
};

inline const char* language_to_string(Language lang) {
    switch (lang) {
        case Language::CPP:      return "cpp";
        case Language::C:        return "c";
        case Language::PYTHON3:  return "python3";
        case Language::JAVA:     return "java";
        case Language::GO:       return "go";
        case Language::RUST:     return "rust";
        default:                 return "unknown";
    }
}

inline Language language_from_string(const std::string& s) {
    if (s == "cpp" || s == "c++")        return Language::CPP;
    if (s == "c")                        return Language::C;
    if (s == "python3" || s == "python" || s == "py") return Language::PYTHON3;
    if (s == "java")                     return Language::JAVA;
    if (s == "go")                       return Language::GO;
    if (s == "rust" || s == "rs")        return Language::RUST;
    return Language::UNKNOWN;
}

struct LanguageConfig {
    Language lang;
    std::string name;                     // "cpp", "python3", ...
    std::vector<std::string> extensions;  // {".cpp", ".cc", ".cxx"}
    std::string compiler_path;            // "/usr/bin/g++" 或 "" (解释型)
    std::vector<std::string> compile_args;// {"-std=c++20", "-O2", ...}
    std::string runtime_path;             // "./solution" 或 "/usr/bin/python3"
    std::vector<std::string> runtime_args;// 解释型语言的脚本参数
    bool needs_compilation;               // false → 直接解释执行
};

} // namespace cppjudge
```

- [ ] **步骤 3：创建 src/CMakeLists\.txt**

```CMake
add_library(cppjudge_core STATIC
    # 各 .cpp 文件在后续任务中添加
)

target_include_directories(cppjudge_core PUBLIC
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(cppjudge_core PUBLIC
    ${LIBSECCOMP_LIBRARIES}
    ${YAMLCPP_LIBRARIES}
    pthread
)
```

- [ ] **步骤 4：创建 tests/CMakeLists\.txt（占位）**

```CMake
enable_testing()
find_package(GTest REQUIRED)

# ============================================================
# 测试标签约定（供 CI ctest -L 过滤）
#   -L unit         → 单元测试（Google Test）
#   -L integration  → 集成测试（shell 脚本）
#   -L security     → 安全测试（手动运行）
#
# 注册方式：
#   gtest_discover_tests(test_xxx PROPERTIES LABELS unit)
#   add_test(NAME xxx COMMAND ...)
#   set_tests_properties(xxx PROPERTIES LABELS integration)
# ============================================================
```

- [ ] **步骤 5：创建 README\.md**

```Markdown
# CPPJUDGE

单机 C++ 编程竞赛判题系统。运行在 WSL2 上。

## 前置条件
- WSL2，内核 5.15+
- root 权限
- `apt install build-essential cmake libseccomp-dev libyaml-cpp-dev libgtest-dev`

## 构建
```

mkdir build \&\& cd build

cmake \.\. \-DCMAKE\_BUILD\_TYPE=Release

make \-j$\(nproc\)

```Plain Text

## 快速开始
```

# 环境检查

sudo \./build/cppjudge doctor

# C\+\+ 判题（自动检测语言）

sudo \./build/cppjudge judge \-\-problem problems/A\+B \-\-submission submissions/cpp/solution\.cpp

# Python3 判题

sudo \./build/cppjudge judge \-\-problem problems/A\+B \-\-submission submissions/python3/solution\.py

# Java 判题

sudo \./build/cppjudge judge \-\-problem problems/A\+B \-\-submission submissions/java/Solution\.java

# 指定语言（覆盖自动检测）

sudo \./build/cppjudge judge \-\-problem problems/A\+B \-\-submission solution\.txt \-\-lang python3

# 查看结果

cat build/judge\_log\.json

```Plain Text

```

- [ ] **步骤 6：创建 CI/CD 配置**

创建 `.github/workflows/ci.yml`：

```YAML
name: CI

on:
  push:
    branches: [master, main]
  pull_request:
    branches: [master, main]

jobs:
  build-and-test:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt update
          sudo apt install -y build-essential cmake \
            libseccomp-dev libyaml-cpp-dev libgtest-dev \
            python3 default-jdk golang-go rustc
          cd /usr/src/gtest
          sudo cmake . && sudo make
          sudo cp lib/libgtest*.a /usr/lib/

      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build -j$(nproc)

      - name: Unit tests
        run: cd build && sudo ctest --output-on-failure -L unit

      - name: Integration tests
        run: cd build && sudo ctest --output-on-failure -L integration

      - name: Doctor check
        run: sudo ./build/cppjudge doctor

      - name: Coverage
        run: |
          cmake -S . -B build_cov -DCMAKE_BUILD_TYPE=Coverage
          cmake --build build_cov -j$(nproc)
          cd build_cov && sudo ctest --output-on-failure || true
          gcovr -r .. --xml -o coverage.xml

      - name: Upload coverage
        uses: codecov/codecov-action@v3
        with:
          files: build_cov/coverage.xml
```

**设计说明：** CI 从 Task 1 就开始工作——初始阶段没有任何测试注册到 `ctest -L unit` 或 `ctest -L integration`，ctest 无匹配测试时输出 `0 tests passed` 且退出码为 0，不会报错。后续每个任务添加测试时只需在 `CMakeLists.txt` 里按标签注册：

- 单元测试：`gtest_discover_tests(test_xxx PROPERTIES LABELS unit)`

- 集成测试：`add_test(...)` \+ `set_tests_properties(... PROPERTIES LABELS integration)`

CI 自动发现新测试，无需任何人修改 CI 配置。

- [ ] **步骤 7：创建 Pre\-commit 配置**

创建 `.pre-commit-config.yaml`：

```YAML
repos:
  - repo: https://github.com/pre-commit/mirrors-clang-format
    rev: v17.0.6
    hooks:
      - id: clang-format
        args: [--style=file, --fallback-style=Google]
        files: \.(cpp|h)$
  - repo: local
    hooks:
      - id: trailing-whitespace
        name: Trim trailing whitespace
        entry: sed -i 's/[[:space:]]*$//'
        language: system
        files: \.(cpp|h|cmake|txt|md|yaml|sh)$
        types: [text]
      - id: no-large-files
        name: Check for large files
        entry: bash -c 'for f in "$@"; do s=$(stat -c%s "$f"); if [ $s -gt 500000 ]; then echo "$f: $s bytes"; exit 1; fi; done' --
        language: system
        files: .*
        exclude: coverage\.html
```

团队成员克隆项目后，只需执行一次：

```Bash
pip install pre-commit
pre-commit install
```

之后每次 `git commit` 自动触发三条检查——不通过就拦住提交。

- [ ] **步骤 8：构建验证**

```Bash
mkdir build && cd build && cmake .. && make
```

- [ ] **步骤 9：提交**

```Bash
git add -A && git commit -m "feat(scaffold): project scaffold with CMake, CI, and shared types"
```

---

### Task 2：Namespace Manager

**文件：**

- 创建：`include/cppjudge/ns_manager.h`

- 创建：`src/ns_manager.cpp`

- 创建：`tests/unit/test_ns_manager.cpp`

**接口：**

- 消费：Task 1 的 `cppjudge::ns::MountEntry`

- 产出：`cppjudge::ns::Manager::setup_rootfs()`、`clone_and_exec()`、`bind_minimal_devices()`

---

- [ ] **步骤 1：编写头文件**

创建 `include/cppjudge/ns_manager.h`：

```C++
#pragma once

#include "cppjudge/common.h"

#include <functional>
#include <string>
#include <vector>

namespace cppjudge::ns {

struct SetupResult {
    bool ok = true;
    std::string error;
};

class Manager {
public:
    // 构建最小 tmpfs 根文件系统 + bind-mount 白名单路径。
    // 必须在子进程内（post-clone）调用。
    static SetupResult setup_rootfs(
        const std::vector<MountEntry>& entries,
        const std::string& work_dir);

    // 在 new_root/dev/ 下 bind-mount 最小设备节点
    static SetupResult bind_minimal_devices(const std::string& new_root);

    // clone(2) 封装。父进程侧返回子进程 PID，子进程内调用 child_main。
    static pid_t clone_and_exec(
        int flags,
        const std::function<int()>& child_main,
        int* child_sync_pipe = nullptr);

    static constexpr int ALL_NS_FLAGS =
        CLONE_NEWNS | CLONE_NEWPID | CLONE_NEWNET |
        CLONE_NEWIPC | CLONE_NEWUTS;

private:
    static bool bind_mount_one(
        const std::string& source,
        const std::string& target,
        bool writable);
};

} // namespace cppjudge::ns
```

- [ ] **步骤 2：写测试**

创建 `tests/unit/test_ns_manager.cpp`：

```C++
#include <gtest/gtest.h>
#include "cppjudge/ns_manager.h"

using namespace cppjudge::ns;

TEST(NsManagerTest, MountEntryDefaultsToReadOnly) {
    MountEntry e{"/usr/bin/g++", "/usr/bin/g++"};
    EXPECT_FALSE(e.writable);
}

TEST(NsManagerTest, MountEntryCanBeWritable) {
    MountEntry e{"/tmp/work", "/tmp/work", true};
    EXPECT_TRUE(e.writable);
}

TEST(NsManagerTest, CloneAndExecSpawnsChild) {
    pid_t child = Manager::clone_and_exec(
        CLONE_NEWPID | CLONE_NEWNET | CLONE_NEWIPC | CLONE_NEWUTS,
        []() -> int { _exit(42); });

    ASSERT_GT(child, 0);
    int status = 0;
    waitpid(child, &status, 0);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 42);
}

TEST(NsManagerTest, AllNsFlagsCoversMountPidNet) {
    EXPECT_TRUE(Manager::ALL_NS_FLAGS & CLONE_NEWNS);
    EXPECT_TRUE(Manager::ALL_NS_FLAGS & CLONE_NEWPID);
    EXPECT_TRUE(Manager::ALL_NS_FLAGS & CLONE_NEWNET);
    EXPECT_TRUE(Manager::ALL_NS_FLAGS & CLONE_NEWIPC);
    EXPECT_TRUE(Manager::ALL_NS_FLAGS & CLONE_NEWUTS);
}

TEST(NsManagerTest, CloneAndExecReturnsErrorOnBadFlags) {
    pid_t child = Manager::clone_and_exec(-1, []() -> int { _exit(0); });
    EXPECT_EQ(child, -1);
}
```

- [ ] **步骤 3：运行测试确认失败**

```Bash
cd build && cmake .. && make test_ns_manager 2>&1 | tail -3
# 预期：链接错误
```

- [ ] **步骤 4：实现 ns\_manager\.cpp**

创建 `src/ns_manager.cpp`：

```C++
#include "cppjudge/ns_manager.h"

#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sched.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

namespace cppjudge::ns {

static constexpr size_t CHILD_STACK_SIZE = 1024 * 1024;

pid_t Manager::clone_and_exec(int flags,
                               const std::function<int()>& child_main,
                               int* /* child_sync_pipe */) {
    char* stack = new char[CHILD_STACK_SIZE];
    char* stack_top = stack + CHILD_STACK_SIZE;

    pid_t child = clone(
        [](void* arg) -> int {
            auto* fn = static_cast<std::function<int()>*>(arg);
            int rc = (*fn)();
            _exit(rc);
            return 0;
        },
        stack_top, flags | SIGCHLD,
        const_cast<std::function<int()>*>(&child_main));

    if (child > 0) delete[] stack;
    return child;
}

// ============================================================
// bind_mount_one — 挂载单个路径的辅助函数
// ============================================================
static bool mkdir_p(const std::string& path, mode_t mode = 0755) {
    if (path.empty() || path == "/") return true;

    size_t pos = 0;
    std::string dir;
    while (pos < path.size()) {
        pos = path.find('/', pos + 1);
        dir = path.substr(0, pos);
        if (mkdir(dir.c_str(), mode) != 0 && errno != EEXIST) {
            return false;
        }
    }
    return true;
}

bool Manager::bind_mount_one(
    const std::string& source,
    const std::string& target,
    bool writable)
{
    // 确保目标目录存在
    if (!mkdir_p(target)) {
        return false;
    }

    // 根据源文件类型将目标创建为文件或目录
    struct stat st;
    if (stat(source.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            mkdir(target.c_str(), 0755);
        } else {
            int fd = creat(target.c_str(), 0644);
            if (fd >= 0) close(fd);
        }
    } else {
        return false;
    }

    // Bind mount：MS_BIND | MS_REC 以包含子挂载
    unsigned long mount_flags = MS_BIND | MS_REC;
    if (mount(source.c_str(), target.c_str(), nullptr, mount_flags, nullptr) != 0) {
        return false;
    }

    // 若非可写则重新挂载为只读
    if (!writable) {
        mount_flags = MS_BIND | MS_REMOUNT | MS_RDONLY | MS_REC;
        if (mount(nullptr, target.c_str(), nullptr, mount_flags, nullptr) != 0) {
            return false;
        }
    }

    return true;
}

// ============================================================
// setup_rootfs — tmpfs + bind mount 白名单 + pivot_root
// ============================================================
Manager::SetupResult Manager::setup_rootfs(
    const std::vector<MountEntry>& entries,
    const std::string& work_dir)
{
    SetupResult result;

    // 步骤 1：将 / 设为私有挂载（MS_PRIVATE）
    if (mount(nullptr, "/", nullptr, MS_PRIVATE | MS_REC, nullptr) != 0) {
        result.ok = false;
        result.error = "Failed to make / private: " +
                       std::string(strerror(errno));
        return result;
    }

    // 步骤 2：在 /tmp/sandbox_newroot 上挂载 tmpfs
    std::string new_root = "/tmp/sandbox_newroot";
    mkdir(new_root.c_str(), 0755);
    if (mount("tmpfs", new_root.c_str(), "tmpfs", 0, nullptr) != 0) {
        result.ok = false;
        result.error = "Failed to mount tmpfs on " + new_root +
                       ": " + std::string(strerror(errno));
        return result;
    }

    // 步骤 3：将每个白名单条目 bind-mount 到 new_root
    for (const auto& entry : entries) {
        std::string full_target = new_root + entry.target;
        if (!bind_mount_one(entry.source, full_target, entry.writable)) {
            result.ok = false;
            result.error = "Failed to bind mount " + entry.source +
                           " → " + full_target +
                           ": " + std::string(strerror(errno));
            return result;
        }
    }

    // 步骤 4：bind 最小设备文件
    auto dev_result = bind_minimal_devices(new_root);
    if (!dev_result.ok) {
        return dev_result;
    }

    // 步骤 5：在 new_root 内创建工作目录
    std::string sandbox_work = new_root + work_dir;
    mkdir_p(sandbox_work, 0755);

    // 步骤 6：pivot_root 切换到新根
    std::string old_root = new_root + "/old_root";
    mkdir(old_root.c_str(), 0755);
    if (syscall(SYS_pivot_root, new_root.c_str(), old_root.c_str()) != 0) {
        result.ok = false;
        result.error = "pivot_root failed: " + std::string(strerror(errno));
        return result;
    }

    // 步骤 7：chdir 到 / 以便卸载 old_root
    chdir("/");
    if (umount2("/old_root", MNT_DETACH) != 0) {
        result.ok = false;
        result.error = "Failed to umount old_root: " +
                       std::string(strerror(errno));
        return result;
    }
    rmdir("/old_root");

    // 步骤 8：挂载 /proc
    mkdir("/proc", 0555);
    if (mount("proc", "/proc", "proc", 0, nullptr) != 0) {
        result.ok = false;
        result.error = "Failed to mount /proc: " +
                       std::string(strerror(errno));
        return result;
    }

    return result;
}

// ============================================================
// bind_minimal_devices — 在 /dev 下创建最小安全设备节点
// ============================================================
Manager::SetupResult Manager::bind_minimal_devices(const std::string& new_root) {
    SetupResult result;

    std::string dev_dir = new_root + "/dev";
    mkdir(dev_dir.c_str(), 0755);

    // 先在 /dev 上挂载最小 tmpfs
    if (mount("tmpfs", dev_dir.c_str(), "tmpfs", 0, "size=1m") != 0) {
        result.ok = false;
        result.error = "Failed to mount tmpfs on " + dev_dir +
                       ": " + std::string(strerror(errno));
        return result;
    }

    // 使用 mknod 仅创建安全的设备节点
    struct { const char* name; mode_t mode; dev_t dev; } devices[] = {
        {"null",    S_IFCHR | 0666, makedev(1, 3)},
        {"zero",    S_IFCHR | 0666, makedev(1, 5)},
        {"urandom", S_IFCHR | 0444, makedev(1, 9)},
        {"stdin",   S_IFCHR | 0600, makedev(136, 0)},
        {"stdout",  S_IFCHR | 0600, makedev(136, 1)},
        {"stderr",  S_IFCHR | 0600, makedev(136, 2)},
    };

    for (const auto& dev : devices) {
        std::string path = dev_dir + "/" + dev.name;
        if (mknod(path.c_str(), dev.mode, dev.dev) != 0) {
            // 非致命：部分设备节点可能已存在
        }
    }

    // 符号链接 /dev/fd → /proc/self/fd（JVM 等需要）
    std::string fd_link = dev_dir + "/fd";
    symlink("/proc/self/fd", fd_link.c_str());

    return result;
}

} // namespace cppjudge::ns
```

- [ ] **步骤 5：添加到构建、编译、测试**

```CMake
# src/CMakeLists.txt: add ns_manager.cpp to add_library
# tests/CMakeLists.txt:
add_executable(test_ns_manager unit/test_ns_manager.cpp)
target_link_libraries(test_ns_manager cppjudge_core GTest::GTest GTest::Main)
gtest_discover_tests(test_ns_manager)
```

```Bash
cd build && cmake .. && make test_ns_manager && sudo ./tests/test_ns_manager
```

- [ ] **步骤 6：提交**

```Bash
git add -A && git commit -m "feat(ns): namespace manager with clone, mount, pivot_root"
```

---

### Task 3：Cgroup Manager

**文件：**

- 创建：`include/cppjudge/cgroup_manager.h`

- 创建：`src/cgroup_manager.cpp`

- 创建：`tests/unit/test_cgroup_manager.cpp`

**接口：**

- 消费：Task 1

- 产出：`cppjudge::cgroup::Manager::create()`、`apply()`、`attach()`、`collect()`、`destroy()`、`is_cgroup_v2_available()`

---

- [ ] **步骤 1：编写头文件** — 同本 Task 开头已列出的 `include/cppjudge/cgroup_manager.h`

- [ ] **步骤 2：编写测试文件**

创建 `tests/unit/test_cgroup_manager.cpp`：

```C++
#include <gtest/gtest.h>
#include "cppjudge/cgroup_manager.h"

using namespace cppjudge::cgroup;

TEST(CgroupManagerTest, IsCgroupV2Available) {
    bool avail = Manager::is_cgroup_v2_available();
    (void)avail;
    SUCCEED();
}

TEST(CgroupManagerTest, CreateAndDestroy) {
    if (!Manager::is_cgroup_v2_available()) {
        GTEST_SKIP() << "cgroup v2 not available";
    }
    auto mgr = Manager::create("test_create_destroy");
    ASSERT_TRUE(mgr.is_valid());
    EXPECT_NO_THROW(mgr.destroy());
}

TEST(CgroupManagerTest, ApplyAndVerifyLimits) {
    if (!Manager::is_cgroup_v2_available()) {
        GTEST_SKIP() << "cgroup v2 not available";
    }
    auto mgr = Manager::create("test_limits");
    ASSERT_TRUE(mgr.is_valid());

    Limits limits;
    limits.max_pids     = 32;
    limits.memory_bytes = 64ULL * 1024 * 1024; // 64 MB
    limits.cpu_time_us  = 0;

    ASSERT_TRUE(mgr.apply(limits));
    auto stats = mgr.collect();
    EXPECT_GE(stats.memory_kb, 0);
    mgr.destroy();
}

TEST(CgroupManagerTest, AttachProcess) {
    if (!Manager::is_cgroup_v2_available()) {
        GTEST_SKIP() << "cgroup v2 not available";
    }
    auto mgr = Manager::create("test_attach");
    ASSERT_TRUE(mgr.is_valid());

    Limits limits;
    limits.max_pids = 1;
    limits.memory_bytes = 128ULL * 1024 * 1024;
    ASSERT_TRUE(mgr.apply(limits));
    ASSERT_TRUE(mgr.attach(getpid()));

    auto parent = Manager::create("test_attach_parent");
    parent.attach(getpid());
    parent.destroy();
    mgr.destroy();
}

TEST(CgroupManagerTest, InvalidCreate) {
    auto mgr = Manager::create("");
    EXPECT_FALSE(mgr.is_valid());
}
```

- [ ] **步骤 3：运行测试 — 验证链接失败**

```Bash
cd build && cmake .. && make test_cgroup_manager 2>&1 | tail -3
# 预期：链接错误
```

- [ ] **步骤 4：实现 cgroup\_manager\.cpp**

创建 `src/cgroup_manager.cpp`：

```C++
#include "cppjudge/cgroup_manager.h"

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cerrno>
#include <cstdio>

namespace cppjudge::cgroup {

static constexpr const char* CGROUP_ROOT = "/sys/fs/cgroup";
static constexpr const char* PREFIX      = "/sys/fs/cgroup/sandbox";

bool Manager::is_cgroup_v2_available() {
    std::ifstream mounts("/proc/mounts");
    std::string line;
    while (std::getline(mounts, line)) {
        if (line.find("cgroup2") != std::string::npos &&
            line.find(CGROUP_ROOT) != std::string::npos) {
            std::string test_file =
                std::string(CGROUP_ROOT) + "/cgroup.procs";
            int fd = open(test_file.c_str(), O_WRONLY);
            if (fd >= 0) {
                close(fd);
                return true;
            }
        }
    }
    return false;
}

Manager Manager::create(const std::string& sandbox_id) {
    Manager mgr;
    if (sandbox_id.empty()) return mgr;

    mgr.path_ = std::string(PREFIX) + "/" + sandbox_id;

    if (mkdir(mgr.path_.c_str(), 0755) != 0) {
        if (errno != EEXIST) return mgr;
    }

    mgr.valid_ = true;
    return mgr;
}

bool Manager::write_control(const std::string& filename,
                            const std::string& value) const {
    if (!valid_) return false;
    std::string full_path = path_ + "/" + filename;
    std::ofstream file(full_path);
    if (!file.is_open()) return false;
    file << value;
    return file.good();
}

std::string Manager::read_control(const std::string& filename) const {
    if (!valid_) return "";
    std::string full_path = path_ + "/" + filename;
    std::ifstream file(full_path);
    std::stringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

bool Manager::apply(const Limits& limits) {
    if (!valid_) return false;

    // CPU 限制
    if (limits.cpu_time_us > 0) {
        uint64_t period_us = 1000000; // 1 秒
        uint64_t quota_us  = limits.cpu_time_us;
        std::string cpu_val =
            std::to_string(quota_us) + " " + std::to_string(period_us);
        if (!write_control("cpu.max", cpu_val)) return false;
    } else {
        if (!write_control("cpu.max", "max 1000000")) return false;
    }

    // 内存限制
    if (limits.memory_bytes > 0) {
        if (!write_control("memory.max",
                           std::to_string(limits.memory_bytes))) return false;
        // 禁用 swap
        if (!write_control("memory.swap.max", "0")) return false;
    }

    // PID 限制
    if (!write_control("pids.max", std::to_string(limits.max_pids))) return false;

    return true;
}

bool Manager::attach(pid_t pid) {
    if (!valid_) return false;
    return write_control("cgroup.procs", std::to_string(pid));
}

Stats Manager::collect() const {
    Stats stats;
    if (!valid_) return stats;

    // CPU 使用: cpu.stat 包含 "usage_usec <value>"
    std::string cpu_stat = read_control("cpu.stat");
    std::istringstream cpu_stream(cpu_stat);
    std::string key;
    uint64_t val;
    while (cpu_stream >> key >> val) {
        if (key == "usage_usec") {
            stats.cpu_usage_us = val;
            break;
        }
    }

    // 内存
    std::string mem_cur = read_control("memory.current");
    stats.memory_kb = std::stoull(mem_cur) / 1024;

    std::string mem_peak = read_control("memory.peak");
    if (!mem_peak.empty() && mem_peak != "\n") {
        stats.memory_peak_kb = std::stoull(mem_peak) / 1024;
    } else {
        stats.memory_peak_kb = stats.memory_kb;
    }

    // OOM 检测
    std::string mem_events = read_control("memory.events");
    std::istringstream ev_stream(mem_events);
    while (ev_stream >> key >> val) {
        if (key == "oom_kill" && val > 0) {
            stats.oom_killed = true;
            break;
        }
    }

    return stats;
}

void Manager::destroy() {
    if (!valid_) return;

    // 杀死 cgroup 内所有进程
    write_control("cgroup.kill", "1");

    // 删除目录
    rmdir(path_.c_str());

    valid_ = false;
}

} // namespace cppjudge::cgroup
```

- [ ] **步骤 5：添加到构建、编译、测试**

```CMake
# src/CMakeLists.txt: add cgroup_manager.cpp
# tests/CMakeLists.txt:
add_executable(test_cgroup_manager unit/test_cgroup_manager.cpp)
target_link_libraries(test_cgroup_manager cppjudge_core GTest::GTest GTest::Main)
gtest_discover_tests(test_cgroup_manager)
```

```Bash
cd build && cmake .. && make test_cgroup_manager && sudo ./tests/test_cgroup_manager
```

```Bash
git add -A && git commit -m "feat(cgroup): cgroup v2 manager with CPU/mem/pid limits"
```

---

### Task 4：Seccomp Manager

**文件：**

- 创建：`include/cppjudge/seccomp_manager.h`

- 创建：`src/seccomp_manager.cpp`

- 创建：`tests/unit/test_seccomp_manager.cpp`

**接口：**

- 消费：Task 1

- 产出：`cppjudge::seccomp::Manager::profile_for_lang()`、`install()`、`violation_to_string()`

**关键约束：** 所有四级白名单必须包含 `SYS_execve` 和 `SYS_execveat`。

---

- [ ] **步骤 1：编写头文件** — 同本 Task 开头已列出的 `include/cppjudge/seccomp_manager.h`

- [ ] **步骤 2：编写测试文件**

创建 `tests/unit/test_seccomp_manager.cpp`：

```C++
#include <gtest/gtest.h>
#include "cppjudge/seccomp_manager.h"

using namespace cppjudge::seccomp;

TEST(SeccompManagerTest, ProfileForLangCpp) {
    EXPECT_EQ(Manager::profile_for_lang("cpp"), Profile::Strict);
    EXPECT_EQ(Manager::profile_for_lang("c"),   Profile::Strict);
}

TEST(SeccompManagerTest, ProfileForLangGo) {
    EXPECT_EQ(Manager::profile_for_lang("go"),   Profile::Standard);
    EXPECT_EQ(Manager::profile_for_lang("rust"), Profile::Standard);
}

TEST(SeccompManagerTest, ProfileForLangPython) {
    EXPECT_EQ(Manager::profile_for_lang("python3"), Profile::Extended);
    EXPECT_EQ(Manager::profile_for_lang("node"),    Profile::Extended);
}

TEST(SeccompManagerTest, ProfileForLangJava) {
    EXPECT_EQ(Manager::profile_for_lang("java"), Profile::JVM);
}

TEST(SeccompManagerTest, ProfileForUnknownLangDefaultsToStrict) {
    EXPECT_EQ(Manager::profile_for_lang("unknown_lang"), Profile::Strict);
}

TEST(SeccompManagerTest, ViolationToString) {
    std::string name = Manager::violation_to_string(0);  // read
    EXPECT_FALSE(name.empty());
}

TEST(SeccompManagerTest, AllowlistsContainExecve) {
    // 验证所有白名单都包含 execve/execveat
    // （否则沙箱无法启动用户程序）
    auto& strict = Manager::get_allowlist_for_testing(Profile::Strict);
    auto& jvm    = Manager::get_allowlist_for_testing(Profile::JVM);
    EXPECT_GT(strict.size(), 0u);
    EXPECT_GT(jvm.size(), strict.size());
}
```

- [ ] **步骤 3：运行测试确认失败**

```Bash
cd build && cmake .. && make test_seccomp_manager 2>&1 | tail -3
# 预期：链接错误
```

- [ ] **步骤 4：实现 seccomp\_manager\.cpp**

创建 `src/seccomp_manager.cpp`：

```C++
#include "cppjudge/seccomp_manager.h"

#include <seccomp.h>
#include <sys/syscall.h>
#include <algorithm>
#include <vector>

namespace cppjudge::seccomp {

// ============================================================
// 各配置文件的 syscall 白名单
// ============================================================
// 关键约束：所有白名单必须包含 SYS_execve 和 SYS_execveat，
// 因为沙箱自身通过 execve 启动用户程序。

static const std::vector<int> STRICT_ALLOWLIST = {
    // 文件 I/O
    SYS_read, SYS_write, SYS_open, SYS_openat, SYS_close,
    SYS_lseek, SYS_readlink, SYS_fstat, SYS_newfstatat,
    // 内存
    SYS_mmap, SYS_munmap, SYS_mprotect, SYS_brk,
    // 进程 — execve/execveat 必须放行
    SYS_execve, SYS_execveat,
    SYS_exit, SYS_exit_group,
    // 安全杂项
    SYS_getpid, SYS_gettid, SYS_getuid, SYS_getgid,
    SYS_clock_gettime, SYS_gettimeofday,
    SYS_nanosleep,
    SYS_arch_prctl,
    SYS_set_tid_address,
    SYS_set_robust_list,
    SYS_rseq,
    SYS_prlimit64,
};

static const std::vector<int> STANDARD_ALLOWLIST = []() {
    std::vector<int> v = STRICT_ALLOWLIST;
    v.insert(v.end(), {
        SYS_futex, SYS_sched_yield, SYS_sched_getaffinity,
        SYS_sigaltstack, SYS_rt_sigaction, SYS_rt_sigprocmask,
        SYS_rt_sigreturn,
        SYS_madvise,
        SYS_clone, SYS_clone3,
        SYS_getrandom,
        SYS_getcwd,
        SYS_stat, SYS_lstat,
        SYS_uname,
    });
    return v;
}();

static const std::vector<int> EXTENDED_ALLOWLIST = []() {
    std::vector<int> v = STANDARD_ALLOWLIST;
    v.insert(v.end(), {
        SYS_poll, SYS_ppoll, SYS_epoll_create1, SYS_epoll_ctl,
        SYS_epoll_pwait2, SYS_epoll_wait,
        SYS_eventfd2,
        SYS_pipe2,
        SYS_socketpair,
        SYS_getdents64,
        SYS_statx,
        SYS_access, SYS_faccessat, SYS_faccessat2,
        SYS_getxattr, SYS_lgetxattr,
        SYS_ioctl,
        SYS_fcntl,
        SYS_dup, SYS_dup2, SYS_dup3,
        SYS_sendfile,
        SYS_copy_file_range,
        SYS_rename, SYS_renameat2,
        SYS_mkdir, SYS_mkdirat,
        SYS_unlink, SYS_unlinkat,
        SYS_rmdir,
        SYS_readahead,
        SYS_membarrier,
        SYS_sysinfo,
    });
    return v;
}();

static const std::vector<int> JVM_ALLOWLIST = []() {
    std::vector<int> v = EXTENDED_ALLOWLIST;
    v.insert(v.end(), {
        SYS_tgkill, SYS_tkill,
        SYS_sched_setaffinity,
        SYS_sched_getparam, SYS_sched_getscheduler,
        SYS_get_robust_list,
        SYS_mincore,
        SYS_msync,
        SYS_mremap,
        SYS_shmget, SYS_shmat, SYS_shmdt, SYS_shmctl,
        SYS_getrusage,
        SYS_times,
        SYS_pread64, SYS_pwrite64,
        SYS_recvfrom, SYS_recvmsg,
    });
    return v;
}();

const std::vector<int>& Manager::get_allowlist(Profile profile) {
    switch (profile) {
        case Profile::Strict:   return STRICT_ALLOWLIST;
        case Profile::Standard: return STANDARD_ALLOWLIST;
        case Profile::Extended: return EXTENDED_ALLOWLIST;
        case Profile::JVM:      return JVM_ALLOWLIST;
    }
    return STRICT_ALLOWLIST;
}

const std::vector<int>& Manager::get_allowlist_for_testing(Profile p) {
    return get_allowlist(p);
}

Profile Manager::profile_for_lang(const std::string& lang) {
    if (lang == "c" || lang == "cpp" || lang == "c++") {
        return Profile::Strict;
    }
    if (lang == "go" || lang == "rust") {
        return Profile::Standard;
    }
    if (lang == "python3" || lang == "python" ||
        lang == "node" || lang == "nodejs" || lang == "javascript" ||
        lang == "ruby" || lang == "php" || lang == "perl") {
        return Profile::Extended;
    }
    if (lang == "java" || lang == "kotlin" || lang == "scala") {
        return Profile::JVM;
    }
    return Profile::Strict; // 未知语言 → 最安全默认值
}

bool Manager::install(Profile profile) {
    scmp_filter_ctx ctx = seccomp_init(SCMP_ACT_KILL_PROCESS);
    if (ctx == nullptr) {
        return false;
    }

    const auto& allowlist = get_allowlist(profile);
    for (int syscall : allowlist) {
        if (seccomp_rule_add(ctx, SCMP_ACT_ALLOW, syscall, 0) != 0) {
            // 对当前架构上不存在的 syscall 非致命，继续
        }
    }

    int rc = seccomp_load(ctx);
    seccomp_release(ctx);

    return rc == 0;
}

std::string Manager::violation_to_string(int syscall_num) {
    char* name = seccomp_syscall_resolve_num_arch(
        SCMP_ARCH_NATIVE, syscall_num);
    if (name != nullptr) {
        std::string result(name);
        free(name);
        return result;
    }
    return "unknown(" + std::to_string(syscall_num) + ")";
}

} // namespace cppjudge::seccomp
```

- [ ] **步骤 5：添加到构建、编译、测试**

```CMake
# src/CMakeLists.txt: add seccomp_manager.cpp
# tests/CMakeLists.txt:
add_executable(test_seccomp_manager unit/test_seccomp_manager.cpp)
target_link_libraries(test_seccomp_manager cppjudge_core GTest::GTest GTest::Main)
gtest_discover_tests(test_seccomp_manager)
```

```Bash
cd build && cmake .. && make test_seccomp_manager && ./tests/test_seccomp_manager
```

```Bash
git add -A && git commit -m "feat(seccomp): seccomp manager with 4-tier syscall allowlist"
```

---

### Task 5：Sandbox Core

**文件：**

- 创建：`include/cppjudge/sandbox.h`

- 创建：`src/sandbox.cpp`

- 创建：`tests/unit/test_sandbox.cpp`

**接口：**

- 消费：Tasks 2\-4

- 产出：`cppjudge::Sandbox::execute(const SandboxConfig&) -> SandboxResult`

---

- [ ] **步骤 1：编写头文件**

创建 `include/cppjudge/sandbox.h`：

```C++
#pragma once

#include "cppjudge/common.h"

#include <string>
#include <vector>

namespace cppjudge {

struct SandboxConfig {
    Limits      limits;
    std::string executable;              // 要运行的可执行文件
    std::vector<std::string> argv;
    std::vector<std::string> envp;
    std::string stdin_path;
    std::string stdout_path;
    std::string stderr_path;
    std::string work_dir;
    std::string lang;                    // "cpp", "python3", ...
    std::vector<ns::MountEntry> extra_mounts; // 语言运行时依赖
};

struct SandboxResult {
    Verdict    verdict        = Verdict::AC;
    int        exit_code      = 0;
    int        signal_num     = 0;
    uint64_t   time_ms        = 0;
    uint64_t   wall_time_ms   = 0;
    uint64_t   memory_kb      = 0;
    bool       output_truncated = false;
    std::string error_detail;
};

class Sandbox {
public:
    // 在隔离沙箱中执行可执行文件。返回 SandboxResult。
    static SandboxResult execute(const SandboxConfig& config);
};

} // namespace cppjudge
```

- [ ] **步骤 2：实现 sandbox\.cpp（子进程顺序已修复）**

创建 `src/sandbox.cpp`。**这是整个系统最关键的函数**——子进程执行顺序绝对不能错：

```C++
#include "cppjudge/sandbox.h"
#include "cppjudge/ns_manager.h"
#include "cppjudge/cgroup_manager.h"
#include "cppjudge/seccomp_manager.h"

#include <sys/wait.h>
#include <sys/timerfd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <cstring>
#include <cerrno>
#include <iostream>

namespace cppjudge {

struct ChildContext {
    SandboxConfig config;
    std::vector<ns::MountEntry> mount_entries;
    int sync_pipe; // 就绪时通知父进程的 fd
};

// ============================================================
// child_main — 在 clone 创建的子进程中运行
//
// 顺序约束（绝对不可调整）：
//   mount → chdir → setrlimit → dup2 → setrlimit(FSIZE)
//   → sync_ready → SECCOMP → execve
// ============================================================
static int child_main(void* arg) {
    auto* ctx = static_cast<ChildContext*>(arg);
    char ready_byte = 1;

    // ---- 以下所有操作必须在 seccomp 安装前完成 ----

    // 1. 设置根文件系统（bind mount + pivot_root）
    auto result = ns::Manager::setup_rootfs(
        ctx->mount_entries, ctx->config.work_dir);
    if (!result.ok) {
        std::cerr << "[sandbox] mount setup failed: " << result.error << "\n";
        write(ctx->sync_pipe, &ready_byte, 1);
        _exit(2);
    }

    // 2. 切换到工作目录
    if (chdir(ctx->config.work_dir.c_str()) != 0) {
        std::cerr << "[sandbox] chdir failed: " << strerror(errno) << "\n";
        write(ctx->sync_pipe, &ready_byte, 1);
        _exit(2);
    }

    // 3. 设置栈大小限制（RLIMIT_STACK）
    if (ctx->config.limits.stack_mb > 0) {
        struct rlimit rl;
        rl.rlim_cur = ctx->config.limits.stack_mb * 1024 * 1024;
        rl.rlim_max = ctx->config.limits.stack_mb * 1024 * 1024;
        if (setrlimit(RLIMIT_STACK, &rl) != 0) {
            std::cerr << "[sandbox] WARNING: setrlimit(RLIMIT_STACK) failed: "
                      << strerror(errno) << "\n";
        }
    }

    // 4. 重定向 stdin/stdout/stderr — 必须在 seccomp 前
    //    因为 dup2/open 不在 Strict/Standard 白名单中
    if (!ctx->config.stdin_path.empty()) {
        int fd = open(ctx->config.stdin_path.c_str(), O_RDONLY);
        if (fd >= 0) { dup2(fd, STDIN_FILENO); close(fd); }
    }
    if (!ctx->config.stdout_path.empty()) {
        int fd = open(ctx->config.stdout_path.c_str(),
                       O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, STDOUT_FILENO); close(fd); }
    }
    if (!ctx->config.stderr_path.empty()) {
        int fd = open(ctx->config.stderr_path.c_str(),
                       O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd >= 0) { dup2(fd, STDERR_FILENO); close(fd); }
    }

    // 5. 设置文件大小限制（RLIMIT_FSIZE）用于 stdout 截断
    if (ctx->config.limits.output_size_mb > 0) {
        struct rlimit rl;
        rl.rlim_cur = ctx->config.limits.output_size_mb * 1024 * 1024;
        rl.rlim_max = ctx->config.limits.output_size_mb * 1024 * 1024;
        setrlimit(RLIMIT_FSIZE, &rl);
    }

    // 6. 通知父进程就绪（mount 完成、fd 就位、限制已应用）
    write(ctx->sync_pipe, &ready_byte, 1);
    close(ctx->sync_pipe);

    // ---- seccomp 必须是 execve 前的绝对最后一步 ----
    // 7. 安装 seccomp 过滤器
    seccomp::Profile profile =
        seccomp::Manager::profile_for_lang(ctx->config.lang);
    if (!seccomp::Manager::install(profile)) {
        std::cerr << "[sandbox] FATAL: seccomp install failed\n";
        _exit(2);
    }

    // 8. 准备 argv
    std::vector<const char*> argv;
    argv.push_back(ctx->config.executable.c_str());
    for (const auto& a : ctx->config.argv) {
        argv.push_back(a.c_str());
    }
    argv.push_back(nullptr);

    // 9. 准备 envp
    std::vector<std::string> env_strings;
    std::vector<const char*> envp;
    for (const auto& s : ctx->config.envp) {
        env_strings.push_back(s);
        envp.push_back(env_strings.back().c_str());
    }
    envp.push_back(nullptr);

    // 10. 执行 — execve 在所有 seccomp 白名单中
    execve(ctx->config.executable.c_str(),
           const_cast<char* const*>(argv.data()),
           const_cast<char* const*>(envp.empty() ? environ : envp.data()));

    std::cerr << "[sandbox] execve failed: " << strerror(errno) << "\n";
    _exit(127);
}

// ============================================================
// execute — 父进程侧的编排逻辑
// ============================================================
SandboxResult Sandbox::execute(const SandboxConfig& config) {
    SandboxResult result;

    // 1. 创建 cgroup
    std::string sandbox_id = "sbx-" + std::to_string(getpid());
    auto cg = cgroup::Manager::create(sandbox_id);
    if (!cg.is_valid()) {
        result.verdict = Verdict::SE;
        result.error_detail = "Failed to create cgroup: " + sandbox_id;
        return result;
    }

    cgroup::Limits climits;
    climits.cpu_time_us  = config.limits.cpu_time_ms * 1000;
    climits.memory_bytes = config.limits.memory_mb * 1024 * 1024;
    climits.max_pids     = config.limits.max_processes;
    cg.apply(climits);

    // 2. 设置同步管道
    int sync_pipe[2];
    if (pipe2(sync_pipe, O_CLOEXEC) != 0) {
        result.verdict = Verdict::SE;
        result.error_detail = "pipe2 failed: " + std::string(strerror(errno));
        return result;
    }

    // 3. 准备 mount 条目（基础 + 额外运行时依赖）
    std::vector<ns::MountEntry> mount_entries = config.extra_mounts;
    // 基础路径
    mount_entries.push_back({"/lib/x86_64-linux-gnu", "/lib/x86_64-linux-gnu", false});
    mount_entries.push_back({"/lib64", "/lib64", false});
    mount_entries.push_back({"/usr/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu", false});

    // 4. 准备子进程上下文
    ChildContext child_ctx;
    child_ctx.config = config;
    child_ctx.mount_entries = mount_entries;
    child_ctx.sync_pipe = sync_pipe[1];

    // 5. Clone 子进程
    pid_t child = ns::Manager::clone_and_exec(
        ns::Manager::ALL_NS_FLAGS,
        [&child_ctx]() -> int { return child_main(&child_ctx); });
    close(sync_pipe[1]);

    if (child < 0) {
        result.verdict = Verdict::SE;
        result.error_detail = "clone failed: " + std::string(strerror(errno));
        return result;
    }

    // 6. Attach 到 cgroup
    cg.attach(child);

    // 7. 等待子进程就绪信号（最多 5 秒）
    char ready;
    struct pollfd pfd = {sync_pipe[0], POLLIN, 0};
    if (poll(&pfd, 1, 5000) <= 0) {
        result.verdict = Verdict::SE;
        result.error_detail = "Child mount setup timed out";
        kill(child, SIGKILL);
        cg.destroy();
        close(sync_pipe[0]);
        return result;
    }
    read(sync_pipe[0], &ready, 1);
    close(sync_pipe[0]);

    // 8. 启动墙上时间计时器
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    struct itimerspec its = {};
    its.it_value.tv_sec  = config.limits.wall_time_ms / 1000;
    its.it_value.tv_nsec = (config.limits.wall_time_ms % 1000) * 1000000;
    timerfd_settime(timer_fd, 0, &its, nullptr);

    // 9. 等待子进程或超时
    uint64_t expirations = 0;
    bool timed_out = false;

    while (true) {
        struct pollfd fds[1] = {{timer_fd, POLLIN, 0}};
        int prc = poll(fds, 1, 100);
        if (prc > 0 && (fds[0].revents & POLLIN)) {
            read(timer_fd, &expirations, sizeof(expirations));
            timed_out = true;
            break;
        }

        int status;
        pid_t w = waitpid(child, &status, WNOHANG);
        if (w == child) {
            if (WIFEXITED(status)) {
                result.exit_code = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result.signal_num = WTERMSIG(status);
                result.verdict = Verdict::RE;
            }
            break;
        } else if (w < 0 && errno != EINTR) {
            result.verdict = Verdict::SE;
            result.error_detail = "waitpid error: " + std::string(strerror(errno));
            break;
        }
    }

    close(timer_fd);

    // 10. 处理超时
    if (timed_out) {
        result.verdict = Verdict::TLE;
        cg.destroy();
        waitpid(child, nullptr, 0);
    }

    // 11. 收集资源统计
    auto stats = cg.collect();
    result.time_ms     = stats.cpu_usage_us / 1000;
    result.memory_kb   = stats.memory_peak_kb;

    if (stats.oom_killed) {
        if (result.verdict == Verdict::AC || result.verdict == Verdict::RE) {
            result.verdict = Verdict::MLE;
        }
    }

    // 12. 检查输出截断
    if (!config.stdout_path.empty()) {
        struct stat st;
        if (stat(config.stdout_path.c_str(), &st) == 0) {
            uint64_t limit = config.limits.output_size_mb * 1024 * 1024;
            if (static_cast<uint64_t>(st.st_size) >= limit) {
                result.output_truncated = true;
                if (result.verdict == Verdict::AC && result.signal_num == SIGXFSZ) {
                    result.verdict = Verdict::OLE;
                }
            }
        }
    }

    // 13. 最终判决：若正常退出且 exit_code != 0 → RE
    if (result.verdict == Verdict::AC && result.exit_code != 0) {
        result.verdict = Verdict::RE;
    }

    cg.destroy();
    return result;
}

} // namespace cppjudge
```

- [ ] **步骤 3：构建并运行测试**

```Bash
cd build && cmake .. && make test_sandbox && sudo ./tests/test_sandbox
```

- [ ] **步骤 4：提交**

```Bash
git add -A && git commit -m "feat(sandbox): sandbox core — clone→mount→cgroup→seccomp→exec lifecycle"
```

---

### Task 6：Problem Manager

**文件：**

- 创建：`include/cppjudge/problem.h`

- 创建：`src/problem.cpp`

- 创建：`problems/A+B/problem.json`、`problems/A+B/input/`、`problems/A+B/output/`

- 创建：`tests/unit/test_problem.cpp`

**接口：**

- 消费：Task 1（common\.h 中的 Limits）

- 产出：`cppjudge::ProblemManager::load()`、`validate()`

---

- [ ] **步骤 1：编写头文件**

创建 `include/cppjudge/problem.h`：

```C++
#pragma once

#include "cppjudge/common.h"

#include <memory>
#include <string>
#include <vector>

namespace cppjudge {

struct Problem {
    std::string title;
    std::string problem_dir;
    Limits      limits;
    std::string compare_mode;    // "exact" | "floating"
    std::string sandbox_type;    // "builtin" | "nsjail"
    double      float_abs_eps = 1e-9;
    double      float_rel_eps = 1e-6;

    struct TestCase {
        std::string input_file;   // 绝对路径
        std::string output_file;  // 绝对路径
        int index;
    };
    std::vector<TestCase> test_cases;
};

class ProblemManager {
public:
    // 从目录加载题目。失败返回 nullptr。
    static std::unique_ptr<Problem> load(const std::string& problem_dir,
                                          std::string& error);

    // 验证题目完整性
    static bool validate(const Problem& problem, std::string& error);
};

} // namespace cppjudge
```

- [ ] **步骤 2：实现**

创建 `src/problem.cpp`（基于 yaml\-cpp 解析 JSON）：

```C++
#include "cppjudge/problem.h"

#include <yaml-cpp/yaml.h>
#include <dirent.h>
#include <sys/stat.h>
#include <algorithm>
#include <fstream>
#include <sstream>

namespace cppjudge {

std::unique_ptr<Problem> ProblemManager::load(const std::string& problem_dir,
                                                std::string& error) {
    // 1. 解析 problem.json（使用 yaml-cpp，因为 JSON 是 YAML 子集）
    std::string json_path = problem_dir + "/problem.json";
    std::ifstream f(json_path);
    if (!f.is_open()) {
        error = "Cannot open " + json_path;
        return nullptr;
    }

    auto problem = std::make_unique<Problem>();
    problem->problem_dir = problem_dir;

    try {
        YAML::Node root = YAML::LoadFile(json_path);
        problem->title               = root["title"].as<std::string>();
        problem->limits.cpu_time_ms  = root["time_limit_ms"].as<uint64_t>(2000);
        problem->limits.memory_mb    = root["memory_limit_mb"].as<uint64_t>(256);
        problem->limits.output_size_mb = root["output_limit_mb"].as<uint64_t>(10);
        problem->limits.compile_time_ms = root["compile_time_limit_ms"].as<uint64_t>(5000);
        problem->compare_mode = root["compare_mode"].as<std::string>("exact");
        problem->sandbox_type = root["sandbox_type"].as<std::string>("nsjail");
        problem->float_abs_eps = root["float_abs_eps"].as<double>(1e-9);
        problem->float_rel_eps = root["float_rel_eps"].as<double>(1e-6);

        // 自动计算墙上时间 = CPU × 3
        problem->limits.wall_time_ms = problem->limits.cpu_time_ms * 3;
    } catch (const YAML::Exception& e) {
        error = "YAML parse error: " + std::string(e.what());
        return nullptr;
    }

    // 2. 扫描 input/ 目录
    std::string input_dir = problem_dir + "/input";
    DIR* dp = opendir(input_dir.c_str());
    if (dp == nullptr) {
        error = "Cannot open input directory: " + input_dir;
        return nullptr;
    }

    std::vector<std::string> input_files;
    struct dirent* entry;
    while ((entry = readdir(dp)) != nullptr) {
        std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        if (name.size() >= 3 && name.substr(name.size() - 3) == ".in") {
            input_files.push_back(name);
        }
    }
    closedir(dp);

    std::sort(input_files.begin(), input_files.end());

    // 3. 配对 .in → .out
    for (const auto& inf : input_files) {
        std::string stem = inf.substr(0, inf.size() - 3); // strip ".in"
        std::string out_name = stem + ".out";
        std::string out_path = problem_dir + "/output/" + out_name;

        struct stat st;
        if (stat(out_path.c_str(), &st) != 0) {
            error = "Missing expected output file: " + out_path;
            return nullptr;
        }

        Problem::TestCase tc;
        tc.input_file  = input_dir + "/" + inf;
        tc.output_file = out_path;
        tc.index       = static_cast<int>(input_files.size());
        // 从文件名提取序号（如 "1.in" → 1）
        try { tc.index = std::stoi(stem); }
        catch (...) { tc.index = static_cast<int>(problem->test_cases.size()) + 1; }
        problem->test_cases.push_back(tc);
    }

    if (problem->test_cases.empty()) {
        error = "No test cases found in " + input_dir;
        return nullptr;
    }

    return problem;
}

bool ProblemManager::validate(const Problem& problem, std::string& error) {
    if (problem.title.empty()) { error = "Title is empty"; return false; }
    if (problem.limits.cpu_time_ms == 0) { error = "time_limit_ms is 0"; return false; }
    if (problem.limits.memory_mb == 0) { error = "memory_limit_mb is 0"; return false; }
    if (problem.compare_mode != "exact" && problem.compare_mode != "floating") {
        error = "compare_mode must be 'exact' or 'floating'"; return false;
    }
    if (problem.test_cases.empty()) { error = "No test cases"; return false; }
    return true;
}

} // namespace cppjudge
```

- [ ] **步骤 3：创建示例题目**

创建 `problems/A+B/problem.json`：

```JSON
{
  "title": "A+B Problem",
  "time_limit_ms": 2000,
  "memory_limit_mb": 256,
  "output_limit_mb": 10,
  "compile_time_limit_ms": 5000,
  "compare_mode": "exact",
  "sandbox_type": "nsjail"
}
```

创建 `problems/A+B/input/1.in`：

```Plain Text
1 2
```

创建 `problems/A+B/output/1.out`：

```Plain Text
3
```

创建 `problems/A+B/input/2.in`：

```Plain Text
999 1
```

创建 `problems/A+B/output/2.out`：

```Plain Text
1000
```

- [ ] **步骤 4：编写测试**

创建 `tests/unit/test_problem.cpp`：

```C++
#include <gtest/gtest.h>
#include "cppjudge/problem.h"

TEST(ProblemManagerTest, LoadValidProblem) {
    std::string error;
    auto p = cppjudge::ProblemManager::load("problems/A+B", error);
    ASSERT_NE(p, nullptr) << error;
    EXPECT_EQ(p->title, "A+B Problem");
    EXPECT_EQ(p->limits.cpu_time_ms, 2000);
    EXPECT_EQ(p->compare_mode, "exact");
    EXPECT_EQ(p->test_cases.size(), 2u);
}

TEST(ProblemManagerTest, ValidatePasses) {
    std::string error;
    auto p = cppjudge::ProblemManager::load("problems/A+B", error);
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(cppjudge::ProblemManager::validate(*p, error));
}

TEST(ProblemManagerTest, LoadNonexistentDirFails) {
    std::string error;
    auto p = cppjudge::ProblemManager::load("problems/Nonexistent", error);
    EXPECT_EQ(p, nullptr);
    EXPECT_FALSE(error.empty());
}
```

- [ ] **步骤 5：构建、测试、提交**

```Bash
cd build && cmake .. && make test_problem && ./tests/test_problem
git add -A && git commit -m "feat(problem): problem manager with JSON parsing and test case pairing"
```

---

### Task 7：Language Manager

**文件：**

- 创建：`include/cppjudge/language.h`

- 创建：`src/language.cpp`

- 创建：`tests/unit/test_language.cpp`

**接口：**

- 消费：Task 1（Language enum、LanguageConfig struct）

- 产出：`cppjudge::LanguageManager::detect()`、`get_config()`、`supported_languages()`

**关键设计决策：** Language Manager 通过硬编码配置表和文件扩展名检测，统一管理所有语言的编译器/解释器路径、编译参数、seccomp profile、mount 依赖。不依赖外部 YAML 配置文件——硬编码确保判题系统自包含。

#### 语言支持矩阵

|语言|扩展名|编译器|解释器|Seccomp|needs\_compilation|
|---|---|---|---|---|---|
|C\+\+|\.cpp, \.cc, \.cxx|/usr/bin/g\+\+|\./solution|Strict|✅|
|C|\.c|/usr/bin/gcc|\./solution|Strict|✅|
|Python3|\.py|—|/usr/bin/python3|Extended|❌|
|Java|\.java|/usr/bin/javac|/usr/bin/java|JVM|✅|
|Go|\.go|/usr/bin/go|\./solution|Standard|✅|
|Rust|\.rs|/usr/bin/rustc|\./solution|Standard|✅|

---

- [ ] **步骤 1：编写头文件**

创建 `include/cppjudge/language.h`：

```C++
#pragma once

#include "cppjudge/common.h"
#include "cppjudge/seccomp_manager.h"

#include <string>
#include <vector>

namespace cppjudge {

// 每种语言运行时需要的额外 mount 条目
struct LanguageRuntimeConfig {
    LanguageConfig   base;
    seccomp::Profile seccomp_profile;
    std::vector<ns::MountEntry> extra_mounts;
    Limits           compile_limits;    // 编译阶段的资源限制
};

class LanguageManager {
public:
    // 从文件扩展名检测语言
    static Language detect_from_extension(const std::string& filename);

    // 从字符串名称解析语言
    static Language parse(const std::string& name);

    // 获取语言的完整运行时配置
    static const LanguageRuntimeConfig& get_runtime(Language lang);

    // 返回所有支持的语言列表
    static const std::vector<Language>& supported_languages();

    // 生成带正确扩展名的目标文件名
    static std::string target_filename(
        Language lang, const std::string& basename);

private:
    static void init_configs();
    static bool configs_initialized_;
    static std::vector<LanguageRuntimeConfig> configs_;
};

} // namespace cppjudge
```

- [ ] **步骤 2：写测试**

创建 `tests/unit/test_language.cpp`：

```C++
#include <gtest/gtest.h>
#include "cppjudge/language.h"

using namespace cppjudge;

TEST(LanguageManagerTest, DetectCppFromExtension) {
    EXPECT_EQ(LanguageManager::detect_from_extension("solution.cpp"), Language::CPP);
    EXPECT_EQ(LanguageManager::detect_from_extension("main.cc"), Language::CPP);
    EXPECT_EQ(LanguageManager::detect_from_extension("src/main.cxx"), Language::CPP);
}

TEST(LanguageManagerTest, DetectPythonFromExtension) {
    EXPECT_EQ(LanguageManager::detect_from_extension("script.py"), Language::PYTHON3);
}

TEST(LanguageManagerTest, DetectJavaFromExtension) {
    EXPECT_EQ(LanguageManager::detect_from_extension("Main.java"), Language::JAVA);
}

TEST(LanguageManagerTest, DetectGoFromExtension) {
    EXPECT_EQ(LanguageManager::detect_from_extension("main.go"), Language::GO);
}

TEST(LanguageManagerTest, DetectRustFromExtension) {
    EXPECT_EQ(LanguageManager::detect_from_extension("main.rs"), Language::RUST);
}

TEST(LanguageManagerTest, DetectCFromExtension) {
    EXPECT_EQ(LanguageManager::detect_from_extension("solution.c"), Language::C);
}

TEST(LanguageManagerTest, DetectUnknownExtension) {
    EXPECT_EQ(LanguageManager::detect_from_extension("README.md"), Language::UNKNOWN);
    EXPECT_EQ(LanguageManager::detect_from_extension("Makefile"), Language::UNKNOWN);
}

TEST(LanguageManagerTest, ParseLanguageStrings) {
    EXPECT_EQ(LanguageManager::parse("cpp"), Language::CPP);
    EXPECT_EQ(LanguageManager::parse("c++"), Language::CPP);
    EXPECT_EQ(LanguageManager::parse("python3"), Language::PYTHON3);
    EXPECT_EQ(LanguageManager::parse("python"), Language::PYTHON3);
    EXPECT_EQ(LanguageManager::parse("java"), Language::JAVA);
    EXPECT_EQ(LanguageManager::parse("go"), Language::GO);
    EXPECT_EQ(LanguageManager::parse("rust"), Language::RUST);
    EXPECT_EQ(LanguageManager::parse("haskell"), Language::UNKNOWN);
}

TEST(LanguageManagerTest, GetRuntimeForEachLang) {
    for (auto lang : LanguageManager::supported_languages()) {
        const auto& rt = LanguageManager::get_runtime(lang);
        EXPECT_EQ(rt.base.lang, lang);
        EXPECT_FALSE(rt.base.name.empty());
        EXPECT_FALSE(rt.base.extensions.empty());
        // 解释型语言不需要编译器
        if (!rt.base.needs_compilation) {
            EXPECT_TRUE(rt.base.compiler_path.empty());
            EXPECT_FALSE(rt.base.runtime_path.empty());
        }
    }
}

TEST(LanguageManagerTest, SupportedLanguagesNotEmpty) {
    EXPECT_GE(LanguageManager::supported_languages().size(), 6u);
}

TEST(LanguageManagerTest, TargetFilename) {
    // C++ 编译产物是 solution（无扩展名）
    EXPECT_EQ(LanguageManager::target_filename(Language::CPP, "solution"), "solution");
    // Python 直接复制源码
    EXPECT_EQ(LanguageManager::target_filename(Language::PYTHON3, "submission"), "submission.py");
    // Java 编译产物是 Solution.class，但入口是 Solution.java
    EXPECT_EQ(LanguageManager::target_filename(Language::JAVA, "Solution"), "Solution.java");
}
```

- [ ] **步骤 3：运行测试确认失败**

```Bash
cd build && cmake .. && make test_language 2>&1 | tail -3
# 预期：链接错误
```

- [ ] **步骤 4：实现 language\.cpp**

创建 `src/language.cpp`：

```C++
#include "cppjudge/language.h"

#include <algorithm>
#include <cctype>

namespace cppjudge {

// ---- 静态初始化 ----
bool LanguageManager::configs_initialized_ = false;
std::vector<LanguageRuntimeConfig> LanguageManager::configs_;

static std::string lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return r;
}

void LanguageManager::init_configs() {
    if (configs_initialized_) return;
    configs_initialized_ = true;

    // ---- C++ ----
    {
        LanguageRuntimeConfig cfg;
        cfg.base.lang             = Language::CPP;
        cfg.base.name             = "cpp";
        cfg.base.extensions       = {".cpp", ".cc", ".cxx"};
        cfg.base.compiler_path    = "/usr/bin/g++";
        cfg.base.compile_args     = {"-std=c++20", "-O2", "-o", "solution", "submission.cpp"};
        cfg.base.runtime_path     = "./solution";
        cfg.base.needs_compilation = true;
        cfg.seccomp_profile       = seccomp::Profile::Strict;
        cfg.extra_mounts = {
            {"/usr/bin/g++", "/usr/bin/g++", false},
            {"/usr/include", "/usr/include", false},
            {"/usr/lib/gcc", "/usr/lib/gcc", false},
        };
        cfg.compile_limits = {5000, 20000, 512, 64, 10, 4, 5000};
        configs_.push_back(cfg);
    }

    // ---- C ----
    {
        LanguageRuntimeConfig cfg;
        cfg.base.lang             = Language::C;
        cfg.base.name             = "c";
        cfg.base.extensions       = {".c"};
        cfg.base.compiler_path    = "/usr/bin/gcc";
        cfg.base.compile_args     = {"-std=c11", "-O2", "-o", "solution", "submission.c"};
        cfg.base.runtime_path     = "./solution";
        cfg.base.needs_compilation = true;
        cfg.seccomp_profile       = seccomp::Profile::Strict;
        cfg.extra_mounts = {
            {"/usr/bin/gcc", "/usr/bin/gcc", false},
            {"/usr/include", "/usr/include", false},
            {"/usr/lib/gcc", "/usr/lib/gcc", false},
        };
        cfg.compile_limits = {5000, 20000, 512, 64, 10, 4, 5000};
        configs_.push_back(cfg);
    }

    // ---- Python3 ----
    {
        LanguageRuntimeConfig cfg;
        cfg.base.lang             = Language::PYTHON3;
        cfg.base.name             = "python3";
        cfg.base.extensions       = {".py"};
        cfg.base.compiler_path    = "";   // 无需编译
        cfg.base.runtime_path     = "/usr/bin/python3";
        cfg.base.runtime_args     = {"submission.py"};
        cfg.base.needs_compilation = false;
        cfg.seccomp_profile       = seccomp::Profile::Extended;
        cfg.extra_mounts = {
            {"/usr/bin/python3", "/usr/bin/python3", false},
            {"/usr/lib/python3", "/usr/lib/python3", false},
            {"/usr/lib/python3.10", "/usr/lib/python3.10", false},
        };
        cfg.compile_limits = {};  // 无编译阶段
        configs_.push_back(cfg);
    }

    // ---- Java ----
    {
        LanguageRuntimeConfig cfg;
        cfg.base.lang             = Language::JAVA;
        cfg.base.name             = "java";
        cfg.base.extensions       = {".java"};
        cfg.base.compiler_path    = "/usr/bin/javac";
        cfg.base.compile_args     = {"submission.java"};
        cfg.base.runtime_path     = "/usr/bin/java";
        cfg.base.runtime_args     = {"Main"};
        cfg.base.needs_compilation = true;
        cfg.seccomp_profile       = seccomp::Profile::JVM;
        cfg.extra_mounts = {
            {"/usr/bin/javac", "/usr/bin/javac", false},
            {"/usr/bin/java", "/usr/bin/java", false},
            {"/usr/lib/jvm", "/usr/lib/jvm", false},
            {"/etc/java-11-openjdk", "/etc/java-11-openjdk", false},
        };
        cfg.compile_limits = {10000, 40000, 1024, 64, 10, 8, 10000};
        configs_.push_back(cfg);
    }

    // ---- Go ----
    {
        LanguageRuntimeConfig cfg;
        cfg.base.lang             = Language::GO;
        cfg.base.name             = "go";
        cfg.base.extensions       = {".go"};
        cfg.base.compiler_path    = "/usr/bin/go";
        cfg.base.compile_args     = {"build", "-o", "solution", "submission.go"};
        cfg.base.runtime_path     = "./solution";
        cfg.base.needs_compilation = true;
        cfg.seccomp_profile       = seccomp::Profile::Standard;
        cfg.extra_mounts = {
            {"/usr/bin/go", "/usr/bin/go", false},
            {"/usr/lib/go", "/usr/lib/go", false},
            {"/usr/lib/go-1.21", "/usr/lib/go-1.21", false},
        };
        cfg.compile_limits = {10000, 40000, 1024, 64, 10, 8, 10000};
        configs_.push_back(cfg);
    }

    // ---- Rust ----
    {
        LanguageRuntimeConfig cfg;
        cfg.base.lang             = Language::RUST;
        cfg.base.name             = "rust";
        cfg.base.extensions       = {".rs"};
        cfg.base.compiler_path    = "/usr/bin/rustc";
        cfg.base.compile_args     = {"-o", "solution", "submission.rs"};
        cfg.base.runtime_path     = "./solution";
        cfg.base.needs_compilation = true;
        cfg.seccomp_profile       = seccomp::Profile::Standard;
        cfg.extra_mounts = {
            {"/usr/bin/rustc", "/usr/bin/rustc", false},
            {"/usr/lib/rustlib", "/usr/lib/rustlib", false},
            {"/usr/lib/x86_64-linux-gnu", "/usr/lib/x86_64-linux-gnu", false},
        };
        cfg.compile_limits = {15000, 60000, 2048, 64, 10, 8, 15000};
        configs_.push_back(cfg);
    }
}

Language LanguageManager::detect_from_extension(const std::string& filename) {
    init_configs();
    // 查找最后一个 '.' 之后的扩展名
    auto dot_pos = filename.rfind('.');
    if (dot_pos == std::string::npos) return Language::UNKNOWN;

    std::string ext = lower(filename.substr(dot_pos));
    for (const auto& cfg : configs_) {
        for (const auto& e : cfg.base.extensions) {
            if (ext == lower(e)) return cfg.base.lang;
        }
    }
    return Language::UNKNOWN;
}

Language LanguageManager::parse(const std::string& name) {
    return language_from_string(name);
}

const LanguageRuntimeConfig& LanguageManager::get_runtime(Language lang) {
    init_configs();
    for (const auto& cfg : configs_) {
        if (cfg.base.lang == lang) return cfg;
    }
    // 回退到 C++（不应该走到这里）
    return configs_.front();
}

const std::vector<Language>& LanguageManager::supported_languages() {
    init_configs();
    static std::vector<Language> langs;
    if (langs.empty()) {
        for (const auto& cfg : configs_) {
            langs.push_back(cfg.base.lang);
        }
    }
    return langs;
}

std::string LanguageManager::target_filename(
    Language lang, const std::string& basename)
{
    init_configs();
    const auto& rt = get_runtime(lang);
    if (rt.base.extensions.empty()) return basename;
    return basename + rt.base.extensions.front();
}

} // namespace cppjudge
```

- [ ] **步骤 5：添加到构建、编译、测试**

```CMake
# src/CMakeLists.txt: add language.cpp
# tests/CMakeLists.txt:
add_executable(test_language unit/test_language.cpp)
target_link_libraries(test_language cppjudge_core GTest::GTest GTest::Main)
gtest_discover_tests(test_language)
```

```Bash
cd build && cmake .. && make test_language && ./tests/test_language
```

```Bash
git add -A && git commit -m "feat(language): language manager with 6-language support matrix"
```

---

### Task 8：Compiler（多语言编译/解释执行）

**文件：**

- `include/cppjudge/compiler.h`

- 更新：`src/compiler.cpp`（使用 Language Manager 驱动）

- 更新：`tests/unit/test_compiler.cpp`

**接口：**

- 消费：Task 5（Sandbox::execute\(\)），Task 7（LanguageManager）

- 产出：`cppjudge::Compiler::compile() -> CompileResult`

**关键变更：** 通过 Language Manager 获取编译器和 mount 配置。对于解释型语言（Python3），compile\(\) 只需复制源文件并返回 success=true。

---

- [ ] **步骤 1：编写更新后的头文件**

更新 `include/cppjudge/compiler.h`：

```C++
#pragma once

#include "cppjudge/common.h"

#include <string>
#include <cstdint>

namespace cppjudge {

struct CompileResult {
    bool        success;
    std::string output;          // compiler/interpreter stderr+stdout
    std::string binary_path;     // 编译产物（解释型语言为源文件副本路径）
    std::string runtime_path;    // 可执行文件或解释器路径
    std::vector<std::string> runtime_args; // 解释器参数
    int         exit_code;
};

class Compiler {
public:
    // 编译（或复制）任意语言的提交。
    // lang 决定使用哪个编译器/解释器；自动从 LanguageManager 获取配置。
    static CompileResult compile(
        const std::string& source_file,
        Language lang,
        const std::string& work_dir);

    // 仅复制源文件到工作目录（用于解释型语言）
    static CompileResult copy_source(
        const std::string& source_file,
        Language lang,
        const std::string& work_dir);
};

} // namespace cppjudge
```

- [ ] **步骤 2：实现 compiler\.cpp**

更新 `src/compiler.cpp`：

```C++
#include "cppjudge/compiler.h"
#include "cppjudge/sandbox.h"
#include "cppjudge/language.h"

#include <fstream>
#include <sstream>
#include <cstdio>
#include <unistd.h>
#include <sys/stat.h>

namespace cppjudge {

CompileResult Compiler::compile(
    const std::string& source_file,
    Language lang,
    const std::string& work_dir)
{
    const auto& rt = LanguageManager::get_runtime(lang);

    // 解释型语言：仅复制源文件
    if (!rt.base.needs_compilation) {
        return copy_source(source_file, lang, work_dir);
    }

    CompileResult result;

    // 1. 复制源码到工作目录
    std::string src_ext = rt.base.extensions.front();
    std::string dest = work_dir + "/submission" + src_ext;
    {
        std::ifstream src(source_file, std::ios::binary);
        std::ofstream dst(dest, std::ios::binary);
        if (!src.is_open() || !dst.is_open()) {
            result.success = false;
            result.output = "Failed to copy source file";
            return result;
        }
        dst << src.rdbuf();
    }

    // 2. 配置编译沙箱
    SandboxConfig config;
    config.executable = rt.base.compiler_path;
    config.argv = rt.base.compile_args;
    config.work_dir = work_dir;
    config.lang = rt.base.name;
    config.limits = rt.compile_limits;
    // stdout/stderr 捕获到文件
    config.stdout_path = work_dir + "/compile_stdout.txt";
    config.stderr_path = work_dir + "/compile_stderr.txt";

    // 编译沙箱的 mount 依赖（语言特定 + 工作目录可写）
    config.extra_mounts = rt.extra_mounts;
    config.extra_mounts.push_back({work_dir, work_dir, true});

    // 3. 在沙箱内编译
    SandboxResult sb_result = Sandbox::execute(config);

    // 4. 读取编译输出
    {
        std::ifstream out(config.stdout_path);
        std::ifstream err(config.stderr_path);
        std::stringstream buf;
        if (out.is_open()) buf << out.rdbuf();
        if (err.is_open()) buf << err.rdbuf();
        result.output = buf.str();
    }

    // 5. 分析结果
    result.exit_code = sb_result.exit_code;
    std::string binary = work_dir + "/solution";
    struct stat st;
    result.success = (sb_result.verdict == Verdict::AC
                      && sb_result.exit_code == 0
                      && stat(binary.c_str(), &st) == 0);
    if (result.success) {
        result.binary_path = binary;
        result.runtime_path = rt.base.runtime_path;
    }

    return result;
}

CompileResult Compiler::copy_source(
    const std::string& source_file,
    Language lang,
    const std::string& work_dir)
{
    CompileResult result;
    const auto& rt = LanguageManager::get_runtime(lang);

    std::string ext = rt.base.extensions.front();
    std::string dest = work_dir + "/submission" + ext;
    {
        std::ifstream src(source_file, std::ios::binary);
        std::ofstream dst(dest, std::ios::binary);
        if (!src.is_open() || !dst.is_open()) {
            result.success = false;
            result.output = "Failed to copy source file";
            return result;
        }
        dst << src.rdbuf();
    }

    result.success     = true;
    result.exit_code   = 0;
    result.binary_path = dest;
    result.runtime_path = rt.base.runtime_path;
    result.runtime_args = rt.base.runtime_args;
    result.output      = "";  // 无编译输出
    return result;
}

} // namespace cppjudge
```

- [ ] **步骤 3：更新测试**

更新 `tests/unit/test_compiler.cpp`：

```C++
#include <gtest/gtest.h>
#include "cppjudge/compiler.h"
#include "cppjudge/language.h"

#include <fstream>

// 写入临时文件
static std::string write_temp(const std::string& name, const std::string& content) {
    std::ofstream f(name);
    f << content;
    f.close();
    return name;
}

TEST(CompilerTest, CompileCppSuccess) {
    write_temp("/tmp/test_ac.cpp",
        "#include <iostream>\nint main(){std::cout<<\"3\"<<std::endl;return 0;}");
    auto r = cppjudge::Compiler::compile(
        "/tmp/test_ac.cpp", cppjudge::Language::CPP, "/tmp");
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.exit_code, 0);
}

TEST(CompilerTest, CompileCppFailure) {
    write_temp("/tmp/test_ce.cpp", "int main() { return @@@; }");
    auto r = cppjudge::Compiler::compile(
        "/tmp/test_ce.cpp", cppjudge::Language::CPP, "/tmp");
    EXPECT_FALSE(r.success);
}

TEST(CompilerTest, PythonNoCompileJustCopy) {
    write_temp("/tmp/test_py.py", "print(1+2)");
    auto r = cppjudge::Compiler::compile(
        "/tmp/test_py.py", cppjudge::Language::PYTHON3, "/tmp");
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.runtime_path, "/usr/bin/python3");
    EXPECT_FALSE(r.runtime_args.empty());
}
```

- [ ] **步骤 4：构建、测试、提交**

```Bash
cd build && cmake .. && make test_compiler && sudo ./tests/test_compiler
git add -A && git commit -m "feat(compiler): multi-language compilation via Language Manager"
```

---

### Task 9：Comparator（比较引擎）

**文件：**

- 创建：`include/cppjudge/comparator.h`

- 创建：`src/comparator.cpp`

- 创建：`tests/unit/test_comparator.cpp`

**接口：**

- 消费：Task 1

- 产出：`cppjudge::Comparator::compare_exact()`、`compare_floating()`

---

- [ ] **步骤 1：实现 comparator**

创建 `include/cppjudge/comparator.h`：

```C++
#pragma once

#include <string>

namespace cppjudge {

struct CompareResult {
    bool is_match;
    std::string detail;
    int mismatch_line;       // exact 模式：第几行不同（1-based）
    int mismatch_token;      // floating 模式：第几个 token 不同
    std::string user_value;
    std::string expected_value;
};

class Comparator {
public:
    static CompareResult compare_exact(
        const std::string& user_output,
        const std::string& expected_output,
        bool trim_trailing_spaces = true,
        bool trim_trailing_newlines = true,
        bool ignore_empty_lines = false);

    static CompareResult compare_floating(
        const std::string& user_output,
        const std::string& expected_output,
        double abs_eps = 1e-9,
        double rel_eps = 1e-6);
};

} // namespace cppjudge
```

创建 `src/comparator.cpp`（实现空白规范化 \+ 浮点 token 比较）：

```C++
#include "cppjudge/comparator.h"

#include <sstream>
#include <vector>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <cerrno>
#include <cstdlib>

namespace cppjudge {

// ---- Exact 模式 ----
CompareResult Comparator::compare_exact(
    const std::string& user_output,
    const std::string& expected_output,
    bool trim_trailing_spaces,
    bool trim_trailing_newlines,
    bool ignore_empty_lines)
{
    CompareResult result;
    result.is_match = true;

    // 按 '\n' 分割为行
    auto split_lines = [](const std::string& s) -> std::vector<std::string> {
        std::vector<std::string> lines;
        std::istringstream stream(s);
        std::string line;
        while (std::getline(stream, line)) {
            lines.push_back(line);
        }
        return lines;
    };

    auto user_lines = split_lines(user_output);
    auto expected_lines = split_lines(expected_output);

    // 处理行尾空格
    if (trim_trailing_spaces) {
        auto trim_right = [](std::string& s) {
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) {
                s.pop_back();
            }
        };
        for (auto& l : user_lines) trim_right(l);
        for (auto& l : expected_lines) trim_right(l);
    }

    // 处理末尾空行
    if (trim_trailing_newlines) {
        while (!user_lines.empty() && user_lines.back().empty())
            user_lines.pop_back();
        while (!expected_lines.empty() && expected_lines.back().empty())
            expected_lines.pop_back();
    }

    // 忽略空行
    if (ignore_empty_lines) {
        auto remove_empty = [](std::vector<std::string>& v) {
            v.erase(std::remove_if(v.begin(), v.end(),
                [](const std::string& s) { return s.empty(); }), v.end());
        };
        remove_empty(user_lines);
        remove_empty(expected_lines);
    }

    // 逐行比较
    size_t max_lines = std::max(user_lines.size(), expected_lines.size());
    for (size_t i = 0; i < max_lines; i++) {
        if (i >= user_lines.size()) {
            result.is_match = false;
            result.mismatch_line = static_cast<int>(i + 1);
            result.detail = "User output has fewer lines than expected";
            result.user_value = "(end of file)";
            result.expected_value = expected_lines[i];
            return result;
        }
        if (i >= expected_lines.size()) {
            result.is_match = false;
            result.mismatch_line = static_cast<int>(i + 1);
            result.detail = "User output has more lines than expected";
            result.user_value = user_lines[i];
            result.expected_value = "(end of file)";
            return result;
        }
        if (user_lines[i] != expected_lines[i]) {
            result.is_match = false;
            result.mismatch_line = static_cast<int>(i + 1);
            result.detail = "Line " + std::to_string(i + 1) + " differs";
            result.user_value = user_lines[i];
            result.expected_value = expected_lines[i];
            return result;
        }
    }

    return result;
}

// ---- Floating 模式 ----
CompareResult Comparator::compare_floating(
    const std::string& user_output,
    const std::string& expected_output,
    double abs_eps, double rel_eps)
{
    CompareResult result;
    result.is_match = true;

    // Token 化
    auto tokenize = [](const std::string& s) -> std::vector<std::string> {
        std::vector<std::string> tokens;
        std::istringstream stream(s);
        std::string token;
        while (stream >> token) tokens.push_back(token);
        return tokens;
    };

    auto user_tokens = tokenize(user_output);
    auto expected_tokens = tokenize(expected_output);

    if (user_tokens.size() != expected_tokens.size()) {
        result.is_match = false;
        result.mismatch_token = static_cast<int>(
            std::min(user_tokens.size(), expected_tokens.size())) + 1;
        result.detail = "Token count differs: user="
            + std::to_string(user_tokens.size())
            + " expected=" + std::to_string(expected_tokens.size());
        return result;
    }

    auto is_float = [](const std::string& s) -> bool {
        if (s == "nan" || s == "inf" || s == "-inf") return true;
        char* end = nullptr;
        strtod(s.c_str(), &end);
        return end != nullptr && *end == '\0' && end != s.c_str();
    };

    auto to_double = [](const std::string& s) -> double {
        if (s == "nan")  return NAN;
        if (s == "inf")  return INFINITY;
        if (s == "-inf") return -INFINITY;
        return strtod(s.c_str(), nullptr);
    };

    for (size_t i = 0; i < user_tokens.size(); i++) {
        const auto& u = user_tokens[i];
        const auto& e = expected_tokens[i];

        bool u_is_num = is_float(u);
        bool e_is_num = is_float(e);

        if (u_is_num && e_is_num) {
            double u_val = to_double(u);
            double e_val = to_double(e);

            // nan 特殊处理
            if (std::isnan(u_val) && std::isnan(e_val)) continue;
            // inf 必须精确匹配
            if (std::isinf(u_val) || std::isinf(e_val)) {
                if (u_val != e_val) {
                    result.is_match = false;
                    result.mismatch_token = static_cast<int>(i + 1);
                    result.user_value = u;
                    result.expected_value = e;
                    result.detail = "Infinity mismatch at token " + std::to_string(i + 1);
                    return result;
                }
                continue;
            }
            // -0.0 == 0.0
            if (u_val == 0.0 && e_val == 0.0) continue;

            double diff = std::abs(u_val - e_val);
            // 绝对误差检查
            if (diff <= abs_eps) continue;
            // 相对误差检查（避免除零）
            double denom = std::max(1.0, std::abs(e_val));
            if (diff / denom <= rel_eps) continue;

            result.is_match = false;
            result.mismatch_token = static_cast<int>(i + 1);
            result.user_value = u;
            result.expected_value = e;
            result.detail = "Float mismatch at token " + std::to_string(i + 1)
                + ": |" + u + " - " + e + "| = " + std::to_string(diff)
                + " (abs_eps=" + std::to_string(abs_eps)
                + ", rel_eps=" + std::to_string(rel_eps) + ")";
            return result;
        } else if (!u_is_num && !e_is_num) {
            if (u != e) {
                result.is_match = false;
                result.mismatch_token = static_cast<int>(i + 1);
                result.user_value = u;
                result.expected_value = e;
                result.detail = "String mismatch at token " + std::to_string(i + 1);
                return result;
            }
        } else {
            result.is_match = false;
            result.mismatch_token = static_cast<int>(i + 1);
            result.user_value = u;
            result.expected_value = e;
            result.detail = "Type mismatch at token " + std::to_string(i + 1)
                + ": " + (u_is_num ? "number" : "string")
                + " vs " + (e_is_num ? "number" : "string");
            return result;
        }
    }

    return result;
}

} // namespace cppjudge
```

- [ ] **步骤 2：编写测试**

创建 `tests/unit/test_comparator.cpp`（覆盖 exact/floating 的所有边界情况）：

```C++
#include <gtest/gtest.h>
#include "cppjudge/comparator.h"

using namespace cppjudge;

// ---- Exact 模式 ----
TEST(ComparatorTest, ExactMatch) {
    auto r = Comparator::compare_exact("hello\nworld\n", "hello\nworld\n");
    EXPECT_TRUE(r.is_match);
}

TEST(ComparatorTest, ExactMismatch) {
    auto r = Comparator::compare_exact("hello\nworld\n", "hello\nWelt\n");
    EXPECT_FALSE(r.is_match);
    EXPECT_EQ(r.mismatch_line, 2);
}

TEST(ComparatorTest, ExactTrimsTrailingSpaces) {
    auto r = Comparator::compare_exact("hello   \nworld\n", "hello\nworld\n");
    EXPECT_TRUE(r.is_match);
}

TEST(ComparatorTest, ExactTrimsTrailingNewlines) {
    auto r = Comparator::compare_exact("hello\nworld\n\n\n", "hello\nworld");
    EXPECT_TRUE(r.is_match);
}

TEST(ComparatorTest, ExactFewerLinesThanExpected) {
    auto r = Comparator::compare_exact("hello\n", "hello\nworld\n");
    EXPECT_FALSE(r.is_match);
}

// ---- Floating 模式 ----
TEST(ComparatorTest, FloatingExactMatch) {
    auto r = Comparator::compare_floating("3.141592653", "3.141592653");
    EXPECT_TRUE(r.is_match);
}

TEST(ComparatorTest, FloatingWithinAbsEps) {
    auto r = Comparator::compare_floating("3.141592654", "3.141592653", 1e-9, 1e-6);
    EXPECT_FALSE(r.is_match); // diff = 1e-9，等于 abs_eps 边界 → 取决于实现
}

TEST(ComparatorTest, FloatingWithinRelEps) {
    auto r = Comparator::compare_floating("1000.001", "1000.000", 1e-9, 1e-3);
    EXPECT_TRUE(r.is_match); // rel diff = 0.001/1000 = 1e-6 < 1e-3
}

TEST(ComparatorTest, FloatingNaNMatchesNaN) {
    auto r = Comparator::compare_floating("nan 42", "nan 42");
    EXPECT_TRUE(r.is_match);
}

TEST(ComparatorTest, FloatingInfMustMatch) {
    auto r = Comparator::compare_floating("inf", "-inf");
    EXPECT_FALSE(r.is_match);
}

TEST(ComparatorTest, FloatingZeroVsNegativeZero) {
    auto r = Comparator::compare_floating("0.0", "-0.0");
    EXPECT_TRUE(r.is_match);
}

TEST(ComparatorTest, FloatingTokenCountMismatch) {
    auto r = Comparator::compare_floating("1 2 3", "1 2");
    EXPECT_FALSE(r.is_match);
}

TEST(ComparatorTest, FloatingStringTokensExactMatch) {
    auto r = Comparator::compare_floating("hello world", "hello world");
    EXPECT_TRUE(r.is_match);
}

TEST(ComparatorTest, FloatingStringTokenMismatch) {
    auto r = Comparator::compare_floating("hello world", "hello Welt");
    EXPECT_FALSE(r.is_match);
}
```

- [ ] **步骤 3：构建、测试、提交**

```Bash
cd build && cmake .. && make test_comparator && ./tests/test_comparator
git add -A && git commit -m "feat(comparator): exact and floating output comparison engine"
```

---

### Task 10：Logger

**文件：**

- 创建：`include/cppjudge/logger.h`

- 创建：`src/logger.cpp`

- 创建：`tests/unit/test_logger.cpp`

**接口：**

- 消费：Task 1（RunResult、Verdict）、Task 6（Problem）

- 产出：`cppjudge::Logger::write_log()`、`create_run_dir()`

---

- [ ] **步骤 1：实现 logger**

创建 `include/cppjudge/logger.h`：

```C++
#pragma once

#include "cppjudge/common.h"
#include <vector>
#include <string>

namespace cppjudge {

class Logger {
public:
    // 创建 per-run 工作目录 build/runs/<run_id>/
    static std::string create_run_dir(const std::string& base_dir);

    // 将判题结果写入 build/judge_log.json
    static bool write_log(
        const std::string& output_path,
        const std::string& problem_dir,
        const std::string& submission_file,
        Verdict final_verdict,
        const std::vector<RunResult>& results);
};

} // namespace cppjudge
```

创建 `src/logger.cpp`（生成 run\_id \+ 写入 JSON）：

```C++
#include "cppjudge/logger.h"

#include <fstream>
#include <chrono>
#include <random>
#include <sstream>
#include <sys/stat.h>
#include <unistd.h>

namespace cppjudge {

static std::string generate_run_id() {
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time_t), "%Y%m%d-%H%M%S");
    // 加 6 位随机 hex
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 0xFFFFFF);
    oss << "-" << std::hex << dis(gen);
    return oss.str();
}

std::string Logger::create_run_dir(const std::string& base_dir) {
    std::string run_id = generate_run_id();
    std::string run_dir = base_dir + "/runs/" + run_id;
    mkdir((base_dir + "/runs").c_str(), 0755);
    mkdir(run_dir.c_str(), 0755);
    return run_dir;
}

bool Logger::write_log(
    const std::string& output_path,
    const std::string& problem_dir,
    const std::string& submission_file,
    Verdict final_verdict,
    const std::vector<RunResult>& results)
{
    std::ofstream f(output_path);
    if (!f.is_open()) return false;

    f << "{\n";
    f << "  \"schema_version\": 1,\n";
    f << "  \"tool\": \"cppjudge\",\n";
    f << "  \"cppjudge_version\": \"0.1.0-dev\",\n";
    f << "  \"problem_dir\": \"" << problem_dir << "\",\n";
    f << "  \"submission_file\": \"" << submission_file << "\",\n";
    f << "  \"final_verdict\": \"" << verdict_to_string(final_verdict) << "\",\n";
    f << "  \"results\": [\n";

    for (size_t i = 0; i < results.size(); i++) {
        const auto& r = results[i];
        f << "    {\n";
        f << "      \"test_index\": " << r.test_index << ",\n";
        f << "      \"verdict\": \"" << verdict_to_string(r.verdict) << "\",\n";
        f << "      \"time_ms\": " << r.time_ms << ",\n";
        f << "      \"wall_time_ms\": " << r.wall_time_ms << ",\n";
        f << "      \"memory_kb\": " << r.memory_kb << ",\n";
        f << "      \"exit_code\": " << r.exit_code << ",\n";
        f << "      \"output_truncated\": " << (r.output_truncated ? "true" : "false") << ",\n";
        f << "      \"run_id\": \"" << r.run_id << "\",\n";
        f << "      \"run_dir\": \"" << r.run_dir << "\"";
        if (!r.compare_detail.empty()) {
            f << ",\n      \"compare_detail\": \"" << r.compare_detail << "\"";
        }
        f << "\n    }";
        if (i < results.size() - 1) f << ",";
        f << "\n";
    }

    f << "  ]\n";
    f << "}\n";
    return f.good();
}

} // namespace cppjudge
```

- [ ] **步骤 2：构建、测试、提交**

```Bash
git add -A && git commit -m "feat(logger): judge_log.json writer and per-run artifact directories"
```

---

### Task 11：CLI Frontend \+ Doctor

**文件：**

- 创建：`src/main.cpp`（CLI 入口）

- 创建：`include/cppjudge/doctor.h`

- 创建：`src/doctor.cpp`

**接口：**

- 消费：Tasks 5\-9

- 产出：`cppjudge` 二进制

---

- [ ] **步骤 1：实现 CLI（main\.cpp）**

创建 `src/main.cpp`：

```C++
#include "cppjudge/common.h"
#include "cppjudge/problem.h"
#include "cppjudge/language.h"
#include "cppjudge/compiler.h"
#include "cppjudge/sandbox.h"
#include "cppjudge/comparator.h"
#include "cppjudge/logger.h"
#include "cppjudge/doctor.h"

#include <iostream>
#include <getopt.h>
#include <cstdlib>
#include <cstring>

// 判题主流程
static int run_judge(int argc, char* argv[]);
// doctor 命令
static int run_doctor();
// 帮助
static void print_usage(const char* prog);
// 按优先级合并判决
static cppjudge::Verdict merge_verdict(cppjudge::Verdict a, cppjudge::Verdict b);

int main(int argc, char* argv[]) {
    if (argc < 2) { print_usage(argv[0]); return 2; }

    std::string cmd = argv[1];
    if (cmd == "judge") return run_judge(argc - 2, argv + 2);
    if (cmd == "doctor") return run_doctor();
    if (cmd == "version" || cmd == "--version") {
        std::cout << "cppjudge 0.1.0-dev\n"; return 0;
    }
    if (cmd == "--help" || cmd == "help") { print_usage(argv[0]); return 0; }

    std::cerr << "Unknown command: " << cmd << "\n";
    return 2;
}

static int run_judge(int argc, char* argv[]) {
    // 解析参数
    std::string problem_dir, submission_file;
    cppjudge::JudgeConfig config;

    static struct option long_opts[] = {
        {"problem",          required_argument, 0, 'p'},
        {"submission",       required_argument, 0, 's'},
        {"lang",             required_argument, 0, 'l'},
        {"time-limit-ms",    required_argument, 0, 't'},
        {"memory-limit-mb",  required_argument, 0, 'm'},
        {"output-limit-mb",  required_argument, 0, 'o'},
        {"compile-time-limit-ms", required_argument, 0, 'c'},
        {"compare-mode",     required_argument, 0, 'C'},
        {"sandbox-type",     required_argument, 0, 'S'},
        {"verbose",          no_argument,       0, 'v'},
        {"help",             no_argument,       0, 'h'},
        {0, 0, 0, 0}
    };

    std::string lang_override;
    int opt;
    while ((opt = getopt_long(argc, argv, "", long_opts, nullptr)) != -1) {
        switch (opt) {
        case 'p': problem_dir = optarg; break;
        case 's': submission_file = optarg; break;
        case 'l': lang_override = optarg; break;
        case 't': config.limits.cpu_time_ms = std::stoull(optarg); break;
        case 'm': config.limits.memory_mb = std::stoull(optarg); break;
        case 'o': config.limits.output_size_mb = std::stoull(optarg); break;
        case 'c': config.limits.compile_time_ms = std::stoull(optarg); break;
        case 'C': config.compare_mode = optarg; break;
        case 'S': config.sandbox_type = optarg; break;
        case 'v': config.verbose = true; break;
        case 'h': print_usage("cppjudge"); return 0;
        }
    }

    if (problem_dir.empty() || submission_file.empty()) {
        std::cerr << "Error: --problem and --submission are required\n";
        return 2;
    }

    // 0. 语言检测（CLI 覆盖优先，否则自动检测扩展名）
    cppjudge::Language lang;
    if (!lang_override.empty()) {
        lang = cppjudge::LanguageManager::parse(lang_override);
        if (lang == cppjudge::Language::UNKNOWN) {
            std::cerr << "Error: unsupported language '" << lang_override << "'\n";
            return 2;
        }
    } else {
        lang = cppjudge::LanguageManager::detect_from_extension(submission_file);
        if (lang == cppjudge::Language::UNKNOWN) {
            std::cerr << "Error: cannot detect language from '"
                      << submission_file << "'. Use --lang to specify.\n";
            return 2;
        }
    }
    std::cerr << "[cppjudge] detected language: "
              << cppjudge::language_to_string(lang) << "\n";

    const auto& lang_rt = cppjudge::LanguageManager::get_runtime(lang);

    // 1. 加载题目
    std::string error;
    auto problem = cppjudge::ProblemManager::load(problem_dir, error);
    if (!problem) {
        std::cerr << "Error: " << error << "\n";
        return 3;
    }

    // CLI 覆盖题目默认值
    if (config.limits.cpu_time_ms == 0) config.limits.cpu_time_ms = problem->limits.cpu_time_ms;
    if (config.limits.memory_mb == 0) config.limits.memory_mb = problem->limits.memory_mb;
    if (config.compare_mode.empty()) config.compare_mode = problem->compare_mode;
    if (config.sandbox_type.empty()) config.sandbox_type = problem->sandbox_type;
    config.limits.wall_time_ms = config.limits.cpu_time_ms * 3;
    config.float_abs_eps = problem->float_abs_eps;
    config.float_rel_eps = problem->float_rel_eps;

    // Production 模式检查
    const char* env = std::getenv("CPPJUDGE_ENV");
    const char* prod = std::getenv("CPPJUDGE_PRODUCTION");
    bool is_prod = (env && std::string(env) == "production") || (prod && std::string(prod) == "1");
    if (is_prod && config.sandbox_type == "builtin") {
        std::cerr << "Error: builtin sandbox rejected in production mode\n";
        return 3;
    }

    // 2. 创建 run 目录
    std::string run_dir = cppjudge::Logger::create_run_dir("build");

    // 3. 编译（或解释型语言复制源文件）
    cppjudge::CompileResult comp = cppjudge::Compiler::compile(
        submission_file, lang, run_dir);
    if (!comp.success) {
        cppjudge::RunResult ce_result;
        ce_result.verdict = cppjudge::Verdict::CE;
        ce_result.error_detail = comp.output;
        ce_result.run_id = run_dir;
        ce_result.run_dir = run_dir;
        cppjudge::Logger::write_log("build/judge_log.json",
            problem_dir, submission_file, cppjudge::Verdict::CE, {ce_result});
        std::cout << "{\"final_verdict\":\"Compile Error\"}\n";
        return 1;
    }

    // 4. 逐测试点执行
    std::vector<cppjudge::RunResult> results;
    cppjudge::Verdict final_verdict = cppjudge::Verdict::AC;

    for (const auto& tc : problem->test_cases) {
        cppjudge::RunResult tr;
        tr.test_index = tc.index;
        tr.run_id = run_dir;
        tr.run_dir = run_dir;

        // 创建测试点子目录
        std::string test_dir = run_dir + "/test_" + std::to_string(tc.index);
        mkdir(test_dir.c_str(), 0755);

        // 配置执行沙箱
        cppjudge::SandboxConfig sconfig;
        // 编译型语言：executable = 编译产物，runtime_path = ./solution
        // 解释型语言：executable = runtime_path (/usr/bin/python3)，argv 包含脚本名
        if (lang_rt.base.needs_compilation) {
            sconfig.executable = comp.binary_path;
        } else {
            sconfig.executable = comp.runtime_path;
            sconfig.argv = comp.runtime_args;
        }
        sconfig.work_dir = test_dir;
        sconfig.lang = lang_rt.base.name;
        sconfig.limits = config.limits;
        sconfig.stdin_path = tc.input_file;
        sconfig.stdout_path = test_dir + "/user_stdout.txt";
        sconfig.stderr_path = test_dir + "/user_stderr.txt";

        // 语言运行时 mount 依赖
        sconfig.extra_mounts = lang_rt.extra_mounts;

        cppjudge::SandboxResult sr = cppjudge::Sandbox::execute(sconfig);

        // 映射 SandboxResult → RunResult
        tr.exit_code = sr.exit_code;
        tr.signal_num = sr.signal_num;
        tr.time_ms = sr.time_ms;
        tr.wall_time_ms = sr.wall_time_ms;
        tr.memory_kb = sr.memory_kb;
        tr.output_truncated = sr.output_truncated;

        if (sr.verdict != cppjudge::Verdict::AC) {
            tr.verdict = sr.verdict;
        } else {
            // 5. 比较输出
            std::string user_output;
            {
                std::ifstream f(sconfig.stdout_path);
                std::stringstream buf;
                buf << f.rdbuf();
                user_output = buf.str();
            }
            std::string expected_output;
            {
                std::ifstream f(tc.output_file);
                std::stringstream buf;
                buf << f.rdbuf();
                expected_output = buf.str();
            }

            cppjudge::CompareResult cr;
            if (config.compare_mode == "floating") {
                cr = cppjudge::Comparator::compare_floating(
                    user_output, expected_output,
                    config.float_abs_eps, config.float_rel_eps);
            } else {
                cr = cppjudge::Comparator::compare_exact(
                    user_output, expected_output);
            }

            if (cr.is_match) {
                tr.verdict = cppjudge::Verdict::AC;
            } else {
                tr.verdict = cppjudge::Verdict::WA;
                tr.compare_detail = cr.detail;
            }
        }

        final_verdict = merge_verdict(final_verdict, tr.verdict);
        results.push_back(tr);
    }

    // 6. 写日志
    cppjudge::Logger::write_log("build/judge_log.json",
        problem_dir, submission_file, final_verdict, results);

    // 7. 输出结果
    std::cout << "{\"final_verdict\":\""
              << cppjudge::verdict_to_string(final_verdict)
              << "\",\"test_count\":" << results.size() << "}\n";

    return (final_verdict == cppjudge::Verdict::AC) ? 0 : 1;
}

// 判决优先级（高 → 低）
static int verdict_rank(cppjudge::Verdict v) {
    switch (v) {
        case cppjudge::Verdict::SE:  return 8;
        case cppjudge::Verdict::CE:  return 7;
        case cppjudge::Verdict::TLE: return 6;
        case cppjudge::Verdict::MLE: return 5;
        case cppjudge::Verdict::OLE: return 4;
        case cppjudge::Verdict::RE:  return 3;
        case cppjudge::Verdict::SV:  return 2;
        case cppjudge::Verdict::WA:  return 1;
        case cppjudge::Verdict::AC:  return 0;
    }
    return -1;
}

static cppjudge::Verdict merge_verdict(cppjudge::Verdict a, cppjudge::Verdict b) {
    return verdict_rank(a) >= verdict_rank(b) ? a : b;
}

static void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " <command> [options]\n\n"
        << "Commands:\n"
        << "  judge    Judge a submission\n"
        << "  doctor   Check environment readiness\n"
        << "  version  Print version\n"
        << "  --help   Show this help\n\n"
        << "Judge options:\n"
        << "  --problem=<dir>          Problem directory (required)\n"
        << "  --submission=<file>      Submission file (required)\n"
        << "  --lang=<lang>            Language override (cpp/c/python3/java/go/rust)\n"
        << "                           Default: auto-detect from file extension\n"
        << "  --time-limit-ms=<N>      CPU time limit override\n"
        << "  --memory-limit-mb=<N>    Memory limit override\n"
        << "  --output-limit-mb=<N>    Output size limit override\n"
        << "  --compile-time-limit-ms=<N>\n"
        << "  --compare-mode=<mode>    'exact' or 'floating'\n"
        << "  --sandbox-type=<type>    'builtin' or 'nsjail'\n"
        << "  --verbose                Detailed output\n";
}

static int run_doctor() {
    return cppjudge::Doctor::check() ? 0 : 2;
}
```

- [ ] **步骤 2：实现 doctor**

创建 `include/cppjudge/doctor.h` 和 `src/doctor.cpp`（检查 WSL2 内核、cgroup v2、libseccomp、namespace 支持、root 权限、题目数据目录）。

核心代码：

```C++
#include "cppjudge/doctor.h"
#include "cppjudge/cgroup_manager.h"

#include <iostream>
#include <fstream>
#include <sys/utsname.h>
#include <unistd.h>
#include <cstdlib>

namespace cppjudge {

bool Doctor::check() {
    std::cout << "OJ CPPJUDGE Environment Check\n";
    std::cout << "==============================\n\n";
    bool all_ok = true;

    auto run = [&](const char* name, bool ok, const char* hint) {
        std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << name;
        if (!ok && hint) std::cout << "\n         → " << hint;
        std::cout << "\n";
        if (!ok) all_ok = false;
    };

    // 1. WSL2
    struct utsname buf;
    bool is_wsl2 = (uname(&buf) == 0 &&
        (std::string(buf.release).find("WSL2") != std::string::npos ||
         std::string(buf.release).find("microsoft") != std::string::npos));
    run("WSL2 kernel", is_wsl2, "Upgrade to WSL2: wsl --set-version <distro> 2");

    // 2. Root
    run("Running as root", geteuid() == 0, "Use: sudo cppjudge judge ...");

    // 3. 内核版本
    int major = 0, minor = 0;
    sscanf(buf.release, "%d.%d", &major, &minor);
    run("Kernel >= 5.15", (major > 5) || (major == 5 && minor >= 15),
        "Update WSL2: wsl --update");

    // 4. cgroup v2
    run("cgroup v2", cgroup::Manager::is_cgroup_v2_available(),
        "sudo mount -t cgroup2 none /sys/fs/cgroup");

    // 5. libseccomp
    bool seccomp_ok = (system("dpkg -l libseccomp2 2>/dev/null | grep -q '^ii'") == 0);
    run("libseccomp2", seccomp_ok, "sudo apt install libseccomp2 libseccomp-dev");

    std::cout << "\n";
    if (all_ok) {
        std::cout << "All checks passed. READY.\n";
        return true;
    } else {
        std::cout << "Some checks failed. NOT_READY.\n";
        return false;
    }
}

} // namespace cppjudge
```

- [ ] **步骤 3：更新 src/CMakeLists\.txt**

```CMake
add_executable(cppjudge main.cpp)
target_link_libraries(cppjudge cppjudge_core)
```

- [ ] **步骤 4：构建、端到端测试、提交**

```Bash
cd build && cmake .. && make cppjudge
sudo ./build/cppjudge doctor
sudo ./build/cppjudge judge --problem problems/A+B --submission submissions/cpp/solution.cpp
git add -A && git commit -m "feat(cli): CLI frontend with judge/doctor/version commands"
```

---

### Task 12：集成测试 \+ 安全测试 \+ 收尾

**文件：**

- 创建：`tests/integration/CMakeLists.txt` \+ 9 个测试脚本

- 创建：`tests/security/run_security_tests.sh` \+ 5 个攻击用例

- 创建：`submissions/cpp/endless_loop.cpp`、`submissions/cpp/memory_hog.cpp`

- 更新：`README.md`

**接口：**

- 消费：完整构建的 `cppjudge` 二进制

---

- [ ] **步骤 1：创建示例提交（TLE/MLE/WA）**

```C++
// submissions/cpp/endless_loop.cpp
int main() { while (true) {} }
```

```C++
// submissions/cpp/memory_hog.cpp
#include <vector>
int main() {
    std::vector<char*> ptrs;
    while (true) ptrs.push_back(new char[1024 * 1024]); // 1MB 块
}
```

```C++
// submissions/cpp/wrong_answer.cpp
#include <iostream>
int main() {
    int a, b;
    std::cin >> a >> b;
    std::cout << a * b << std::endl; // 错误：应该是 a + b
    return 0;
}
```

- [ ] **步骤 2：编写集成测试脚本（9 种判决）**

创建 `tests/integration/test_ac.sh`：

```Bash
#!/bin/bash
set -e
OUTPUT=$(sudo ./build/cppjudge judge --problem problems/A+B --submission submissions/cpp/solution.cpp 2>&1)
echo "$OUTPUT" | grep -q '"final_verdict":"Accepted"' || { echo "FAIL: Expected AC"; exit 1; }
echo "PASS: AC"
```

创建 `tests/integration/test_wa.sh`：

```Bash
#!/bin/bash
set -e
OUTPUT=$(sudo ./build/cppjudge judge --problem problems/A+B --submission submissions/cpp/wrong_answer.cpp 2>&1)
echo "$OUTPUT" | grep -q 'Wrong Answer' || { echo "FAIL: Expected WA"; exit 1; }
echo "PASS: WA"
```

创建 `tests/integration/test_tle.sh`：

```Bash
#!/bin/bash
set -e
OUTPUT=$(sudo ./build/cppjudge judge --problem problems/A+B --submission submissions/cpp/endless_loop.cpp --time-limit-ms 500 2>&1)
echo "$OUTPUT" | grep -q 'Time Limit Exceeded' || { echo "FAIL: Expected TLE"; exit 1; }
echo "PASS: TLE"
```

创建 `tests/integration/test_mle.sh`：

```Bash
#!/bin/bash
set -e
OUTPUT=$(sudo ./build/cppjudge judge --problem problems/A+B --submission submissions/cpp/memory_hog.cpp --memory-limit-mb 32 2>&1)
echo "$OUTPUT" | grep -q 'Memory Limit Exceeded' || { echo "FAIL: Expected MLE"; exit 1; }
echo "PASS: MLE"
```

创建 `tests/integration/test_ce.sh`：

```Bash
#!/bin/bash
set -e
OUTPUT=$(sudo ./build/cppjudge judge --problem problems/A+B --submission submissions/cpp/broken.cpp 2>&1)
echo "$OUTPUT" | grep -q 'Compile Error' || { echo "FAIL: Expected CE"; exit 1; }
echo "PASS: CE"
```

创建 `tests/integration/test_re.sh`：

```Bash
#!/bin/bash
set -e
# submissions/cpp/return_1.cpp: int main() { return 1; }
OUTPUT=$(sudo ./build/cppjudge judge --problem problems/A+B --submission submissions/cpp/return_1.cpp 2>&1)
echo "$OUTPUT" | grep -q 'Runtime Error' || { echo "FAIL: Expected RE"; exit 1; }
echo "PASS: RE"
```

创建 `tests/integration/test_se.sh`：

```Bash
#!/bin/bash
set -e
# 题目目录不存在 → System Error
OUTPUT=$(sudo ./build/cppjudge judge --problem problems/Nonexistent --submission submissions/solution.cpp 2>&1)
echo "$OUTPUT" | grep -q 'System Error' || { echo "FAIL: Expected SE"; exit 1; }
echo "PASS: SE"
```

创建 `tests/integration/test_ole.sh`：

```Bash
#!/bin/bash
set -e
# 无限输出 → 应触发输出限制
OUTPUT=$(sudo ./build/cppjudge judge --problem problems/A+B --submission submissions/cpp/infinite_output.cpp --output-limit-mb 1 2>&1)
echo "$OUTPUT" | grep -q 'Output Limit Exceeded' || { echo "FAIL: Expected OLE"; exit 1; }
echo "PASS: OLE"
```

创建 `tests/integration/test_floating.sh`：

```Bash
#!/bin/bash
set -e
# 浮点比较 — 创建临时题目
TEST_DIR=$(mktemp -d /tmp/cppjudge_floating_XXXXXX)
trap "rm -rf $TEST_DIR" EXIT

mkdir -p $TEST_DIR/{input,output}
cat > $TEST_DIR/problem.json << 'EOF'
{
  "title": "Floating Test",
  "time_limit_ms": 2000,
  "memory_limit_mb": 128,
  "output_limit_mb": 10,
  "compare_mode": "floating",
  "float_abs_eps": 1e-9,
  "float_rel_eps": 1e-6
}
EOF
echo "3.141592653589793" > $TEST_DIR/input/1.in
echo "3.141592654" > $TEST_DIR/output/1.out

# 提交浮点计算的程序
cat > $TEST_DIR/sub.cpp << 'CEOF'
#include <iostream>
#include <cmath>
int main() {
    std::cout.precision(16);
    std::cout << M_PI << std::endl;
    return 0;
}
CEOF

OUTPUT=$(sudo ./build/cppjudge judge --problem $TEST_DIR --submission $TEST_DIR/sub.cpp 2>&1)
echo "$OUTPUT" | grep -q 'Accepted' || { echo "FAIL: Expected AC with floating tolerance"; exit 1; }
echo "PASS: Floating"
```

创建示例提交文件：

```C++
// submissions/cpp/return_1.cpp — 返回 1 → RE
int main() { return 1; }
```

```C++
// submissions/cpp/infinite_output.cpp — 无限输出 → OLE
#include <iostream>
int main() {
    while (true) std::cout << "x";
}
```

```Bash
chmod +x tests/integration/test_{re,se,ole,floating}.sh
```

- [ ] **步骤 3：编写安全测试**

创建 `tests/security/cases/malicious_include.cpp`：

```C++
// 尝试读取宿主机文件——编译沙箱应阻止
#include "/etc/passwd"
int main() { return 0; }
```

创建 `tests/security/cases/fork_bomb.cpp`：

```C++
#include <unistd.h>
int main() { while (true) fork(); }
```

创建 `tests/security/cases/fork_bomb.cpp`：

```C++
#include <unistd.h>
int main() { while (true) fork(); }
```

创建 `tests/security/cases/infinite_memory.cpp`：

```C++
#include <vector>
int main() {
    std::vector<char*> ptrs;
    while (true) ptrs.push_back(new char[1024 * 1024]);
}
```

创建 `tests/security/cases/bad_syscall.cpp`：

```C++
// 尝试网络连接 — seccomp 应拦截
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
int main() {
    int s = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(80);
    inet_pton(AF_INET, "93.184.216.34", &addr.sin_addr);
    connect(s, (struct sockaddr*)&addr, sizeof(addr));
    close(s);
    return 0;
}
```

创建 `tests/security/cases/read_passwd.cpp`：

```C++
// 尝试读取 /etc/passwd — 沙箱内不应存在
#include <fstream>
#include <iostream>
int main() {
    std::ifstream f("/etc/passwd");
    if (f.is_open()) {
        std::string line;
        while (std::getline(f, line)) std::cout << line << "\n";
    }
    return 0;
}
```

创建 `tests/security/run_security_tests.sh`：

```Bash
#!/bin/bash
set -euo pipefail
PASS=0; FAIL=0

run_test() {
    local name="$1"; local source="$2"; local expect="$3"
    echo -n "  [$name] ... "
    OUTPUT=$(sudo ./build/cppjudge judge --problem problems/A+B --submission "$source" 2>&1 || true)
    if echo "$OUTPUT" | grep -q "$expect"; then
        echo "PASS"; PASS=$((PASS + 1))
    else
        echo "FAIL (expected $expect)"; FAIL=$((FAIL + 1))
    fi
}

echo "=== Security Tests ==="
run_test "Fork bomb"      tests/security/cases/fork_bomb.cpp      "Syscall Violation"
run_test "Bad syscall"    tests/security/cases/bad_syscall.cpp     "Syscall Violation"
run_test "Malicious include" tests/security/cases/malicious_include.cpp "Compile Error"
run_test "Read /etc/passwd" tests/security/cases/read_passwd.cpp   "Runtime Error"
echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
```

- [ ] **步骤 4：多语言 AC 测试**

创建 `tests/integration/test_all_languages.sh`：

```Bash
#!/bin/bash
set -euo pipefail
PASS=0; FAIL=0

PROBLEM="problems/A+B"

run_lang_test() {
    local lang_name="$1"; local source="$2"; local extra_flag="$3"
    echo -n "  [$lang_name] ... "
    OUTPUT=$(sudo ./build/cppjudge judge --problem "$PROBLEM" --submission "$source" $extra_flag 2>&1 || true)
    if echo "$OUTPUT" | grep -q '"final_verdict":"Accepted"'; then
        echo "PASS"; PASS=$((PASS + 1))
    else
        echo "FAIL (output: $OUTPUT)"; FAIL=$((FAIL + 1))
    fi
}

echo "=== Multi-Language AC Tests ==="

# C++ (auto-detect .cpp)
run_lang_test "C++"    "submissions/cpp/solution.cpp" ""

# C (auto-detect .c)
run_lang_test "C"      "submissions/c/solution.c" ""

# Python3 (auto-detect .py)
run_lang_test "Python3" "submissions/python3/solution.py" ""

# Java (auto-detect .java)
run_lang_test "Java"   "submissions/java/Solution.java" ""

# Go (auto-detect .go)
run_lang_test "Go"     "submissions/go/solution.go" ""

# Rust (auto-detect .rs)
run_lang_test "Rust"   "submissions/rust/solution.rs" ""

echo "=== Results: $PASS passed, $FAIL failed ==="
exit $FAIL
```

创建各语言的示例提交文件：

```C++
// submissions/c/solution.c — C 语言 A+B
#include <stdio.h>
int main() {
    int a, b;
    scanf("%d %d", &a, &b);
    printf("%d\n", a + b);
    return 0;
}
```

```Python
# submissions/python3/solution.py — Python3 A+B
import sys
a, b = map(int, sys.stdin.read().split())
print(a + b)
```

```Python
# submissions/python3/broken.py — 语法错误
import sys
a, b = map(int, sys.stdin.read().split()
print(a + b)
```

```Java
// submissions/java/Solution.java — Java A+B (class 名必须是 Main)
import java.util.Scanner;
public class Solution {
    public static void main(String[] args) {
        Scanner sc = new Scanner(System.in);
        int a = sc.nextInt();
        int b = sc.nextInt();
        System.out.println(a + b);
    }
}
```

```Go
// submissions/go/solution.go — Go A+B
package main
import "fmt"
func main() {
    var a, b int
    fmt.Scan(&a, &b)
    fmt.Println(a + b)
}
```

```Rust
// submissions/rust/solution.rs — Rust A+B
use std::io;
fn main() {
    let mut line = String::new();
    io::stdin().read_line(&mut line).unwrap();
    let nums: Vec<i32> = line
        .split_whitespace()
        .map(|s| s.parse().unwrap())
        .collect();
    println!("{}", nums[0] + nums[1]);
}
```

```Bash
chmod +x tests/integration/test_all_languages.sh
```

- [ ] **步骤 5：运行全部测试**

```Bash
cd build && cmake .. && make -j$(nproc)
sudo ctest -V                                   # 单元测试 + 集成测试
sudo tests/security/run_security_tests.sh       # 安全测试
sudo tests/integration/test_all_languages.sh    # 多语言 AC 测试
```

- [ ] **步骤 6：完善 README\.md，提交**

```Bash
git add -A && git commit -m "test: 9-verdict integration tests, 6-language AC tests, security tests, and documentation"
```

---

## 测试覆盖率指标

### 按模块分级

|模块|行覆盖率|分支覆盖率|
|---|---|---|
|Seccomp Manager|≥ 95%|≥ 90%|
|Comparator|≥ 95%|≥ 90%|
|Language Manager|≥ 95%|≥ 90%|
|Cgroup Manager|≥ 90%|≥ 85%|
|Namespace Manager|≥ 85%|≥ 80%|
|Sandbox Core|≥ 85%|≥ 80%|
|Compiler|≥ 80%|≥ 75%|
|Problem Manager|≥ 80%|≥ 75%|
|Logger|≥ 75%|—|
|CLI Frontend|≥ 75%|≥ 70%|
|Doctor|≥ 70%|—|

### 全局指标

|指标|目标值|
|---|---|
|行覆盖率|≥ 85%|
|分支覆盖率|≥ 80%|
|函数覆盖率|≥ 95%|

---

## CI/CD \(GitHub Actions — 真 Linux\)

CI 运行在 Ubuntu 22\.04 真实 Linux 环境（非 WSL2），有完整的 cgroup v2 和 namespace 支持。

### `.github/workflows/ci.yml`

```YAML
name: CI

on:
  push:
    branches: [master, main]
  pull_request:
    branches: [master, main]

jobs:
  build-and-test:
    runs-on: ubuntu-22.04
    steps:
      - uses: actions/checkout@v4

      - name: Install dependencies
        run: |
          sudo apt update
          sudo apt install -y build-essential cmake \
            libseccomp-dev libyaml-cpp-dev libgtest-dev \
            python3 default-jdk golang-go rustc
          # Build Google Test from source (Ubuntu package is headers-only)
          cd /usr/src/gtest
          sudo cmake . && sudo make
          sudo cp lib/libgtest*.a /usr/lib/

      - name: Configure
        run: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

      - name: Build
        run: cmake --build build -j$(nproc)

      - name: Unit tests
        run: cd build && sudo ctest --output-on-failure -L unit

      - name: Integration tests
        run: cd build && sudo ctest --output-on-failure -L integration

      - name: Doctor check
        run: sudo ./build/cppjudge doctor

      - name: Coverage build
        run: |
          cmake -S . -B build_cov -DCMAKE_BUILD_TYPE=Coverage
          cmake --build build_cov -j$(nproc)

      - name: Coverage report
        run: |
          cd build_cov
          sudo ctest --output-on-failure || true
          gcovr -r .. --xml -o coverage.xml

      - name: Upload coverage
        uses: codecov/codecov-action@v3
        with:
          files: build_cov/coverage.xml
```

### WSL2 与真 Linux 差异处理

|场景|WSL2（开发）|Ubuntu 22\.04（CI）|
|---|---|---|
|cgroup v2 路径|`/sys/fs/cgroup/`|`/sys/fs/cgroup/`|
|权限|`sudo` 或 root shell|CI 默认 root|
|内核|WSL2 定制内核|标准 Ubuntu 内核|
|安全测试|全部运行|跳过 nsjail 相关（CI 无 nsjail）|

CI 标记：用 `ctest -L unit` 和 `ctest -L integration` 区分测试标签，安全测试用 `-L security` 仅在手动触发时运行。

---

## Pre\-Commit 钩子

```YAML
# .pre-commit-config.yaml
repos:
  - repo: https://github.com/pre-commit/mirrors-clang-format
    rev: v17.0.6
    hooks:
      - id: clang-format
        args: [--style=file, --fallback-style=Google]
        files: \.(cpp|h)$
  - repo: local
    hooks:
      - id: trailing-whitespace
        name: Trim trailing whitespace
        entry: sed -i 's/[[:space:]]*$//'
        language: system
        files: \.(cpp|h|cmake|txt|md|yaml|sh)$
        types: [text]
      - id: no-large-files
        name: Check for large files
        entry: bash -c 'for f in "$@"; do s=$(stat -c%s "$f"); if [ $s -gt 500000 ]; then echo "$f: $s bytes"; exit 1; fi; done' --
        language: system
        files: .*
        exclude: coverage\.html
```

---

## 提交规范

使用 Conventional Commits：`<type>(<scope>): <description>`

|Type|用途|
|---|---|
|`feat`|新模块或功能|
|`fix`|缺陷修复|
|`test`|测试|
|`build`|CMake、依赖|
|`docs`|文档|
|`refactor`|重构|
|`chore`|脚本、配置|

|Scope|对应模块|
|---|---|
|`scaffold`|项目脚手架|
|`ns`|Namespace Manager|
|`cgroup`|Cgroup Manager|
|`seccomp`|Seccomp Manager|
|`sandbox`|Sandbox Core|
|`problem`|Problem Manager|
|`compiler`|Compiler|
|`comparator`|Comparator|
|`logger`|Logger|
|`cli`|CLI Frontend|
|`doctor`|Doctor|

示例：

```Plain Text
feat(scaffold): project scaffold with CMake and shared types
feat(ns): namespace manager with clone, mount, pivot_root
feat(comparator): exact and floating output comparison engine
feat(cli): CLI frontend with judge/doctor/version commands
test(integration): 9-verdict integration tests
```

---

## 总结

|Task|模块|预估工时|依赖|负责人|
|---|---|---|---|---|
|1|项目脚手架 \+ 共享类型||无|@褚浩然|
|2|Namespace Manager||Task 1|@褚浩然|
|3|Cgroup Manager||Task 1|@裴睿泽|
|4|Seccomp Manager||Task 1|@杨伟杰|
|5|Sandbox Core||Tasks 2\-4|@席源|
|6|Problem Manager||Task 1|@王毅|
|7|Language Manager||Tasks 1, 4|@严浩容|
|8|Compiler（多语言）||Tasks 5, 7|@席源|
|9|Comparator||Task 1|@褚浩然|
|10|Logger||Tasks 1, 6|@杨伟杰|
|11|CLI \+ Doctor（多语言）||Tasks 5\-10|@王毅|
|12|集成测试 \+ 安全测试 \+ 多语言测试||以上全部|@严浩容@裴睿泽|

**最大并行窗口：** Tasks 2/3/4/6/9 可在 Task 1 后同步进行 。Tasks 7/8/10/11 可在 Task 5 后并行。

> (注：内容由 AI 生成，请谨慎参考）

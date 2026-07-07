#include "cppjudge/language.h"

#include <limits.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace cppjudge {

namespace {

std::string to_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// 从解析出的编译器绝对路径推导需要挂载的工具链根目录。
// resolve_tool 会在 /usr/local/go/bin、$HOME/.cargo/bin 等目录里找到 go/rustc，
// 但沙箱只挂载固定的基础目录（/usr/bin、/usr/lib…）。若工具链位于非基础目录下，
// execve 在沙箱内会 ENOENT → 编译必失败（CE）。此处把「bin 的父目录」（工具链根，
// 含 bin/lib/…）作为额外只读挂载补上；已在基础挂载覆盖范围内的则返回空、无需重复。
// compiler_path 已由 resolve_tool 经 realpath 规范化（rustup 会指向 ~/.rustup/toolchains/…）。
std::vector<ns::MountEntry> toolchain_mounts(const std::string& compiler_path) {
    if (compiler_path.empty() || compiler_path[0] != '/') return {};
    // 基础挂载已覆盖的前缀，无需额外挂载。
    for (const char* base : {"/usr/bin/", "/bin/", "/usr/lib/", "/usr/lib64/",
                             "/lib/", "/lib64/", "/usr/libexec/", "/usr/share/"}) {
        if (compiler_path.rfind(base, 0) == 0) return {};
    }
    // 去掉 "/bin/<exe>"，得到工具链根。
    auto slash = compiler_path.find_last_of('/');
    if (slash == std::string::npos) return {};
    std::string bin_dir = compiler_path.substr(0, slash);         // …/bin
    auto slash2 = bin_dir.find_last_of('/');
    if (slash2 == std::string::npos || slash2 == 0) return {};
    std::string root = bin_dir.substr(0, slash2);                 // 工具链根
    if (root.empty() || root == "/usr" || root == "/") return {};
    return {{root, root, false}};
}

// 编译阶段的默认资源限制：{cpu, wall, mem_mb, stack_mb, out_mb, max_proc, compile_ms}
// 内存/进程给得较宽：go/javac 等编译器自身是大程序、会起多线程。
constexpr Limits kCompileLimits{15000, 60000, 2048, 256, 64, 256, 15000};

std::vector<LanguageRuntimeConfig> build_configs() {
    std::vector<LanguageRuntimeConfig> cfgs;

    {   // C++
        LanguageRuntimeConfig c;
        c.lang = Language::CPP;
        c.name = "cpp";
        c.extensions = {".cpp", ".cc", ".cxx", ".c++"};
        c.needs_compilation = true;
        c.source_name = "submission.cpp";
        c.compiler_path = resolve_tool({"g++", "c++", "clang++"});
        c.compile_args = {"-std=c++20", "-O2", "-o", "solution", "submission.cpp"};
        c.artifact_name = "solution";
        c.seccomp_profile = SeccompProfile::Strict;
        c.extra_mounts = {{"/usr/include", "/usr/include", false},
                          {"/usr/lib/gcc", "/usr/lib/gcc", false}};
        c.compile_limits = kCompileLimits;
        cfgs.push_back(c);
    }
    {   // C
        LanguageRuntimeConfig c;
        c.lang = Language::C;
        c.name = "c";
        c.extensions = {".c"};
        c.needs_compilation = true;
        c.source_name = "submission.c";
        c.compiler_path = resolve_tool({"gcc", "cc", "clang"});
        c.compile_args = {"-std=c11", "-O2", "-o", "solution", "submission.c"};
        c.artifact_name = "solution";
        c.seccomp_profile = SeccompProfile::Strict;
        c.extra_mounts = {{"/usr/include", "/usr/include", false},
                          {"/usr/lib/gcc", "/usr/lib/gcc", false}};
        c.compile_limits = kCompileLimits;
        cfgs.push_back(c);
    }
    {   // Python3
        LanguageRuntimeConfig c;
        c.lang = Language::PYTHON3;
        c.name = "python3";
        c.extensions = {".py"};
        c.needs_compilation = false;
        c.source_name = "submission.py";
        c.interpreter_path = resolve_tool({"python3", "python"});
        c.run_args = {"submission.py"};
        c.seccomp_profile = SeccompProfile::Extended;
        c.extra_mounts = {{"/usr/lib/python3", "/usr/lib/python3", false}};
        c.compile_limits = kCompileLimits;
        cfgs.push_back(c);
    }
    {   // Java —— 入口类统一为 Main（修 Solution/Main 冲突）
        LanguageRuntimeConfig c;
        c.lang = Language::JAVA;
        c.name = "java";
        c.extensions = {".java"};
        c.needs_compilation = true;
        c.source_name = "Main.java";
        c.compiler_path = resolve_tool({"javac"});
        c.compile_args = {"Main.java"};
        c.artifact_name = "Main.class";
        c.interpreter_path = resolve_tool({"java"});
        c.run_args = {"-XX:-UsePerfData", "-cp", ".", "Main"};
        c.seccomp_profile = SeccompProfile::JVM;
        // JVM 依赖：JDK 本体 + Debian 把 java.security 等配置放在 /etc/java-*
        c.extra_mounts = {{"/usr/lib/jvm", "/usr/lib/jvm", false},
                          {"/etc/alternatives", "/etc/alternatives", false},
                          {"/etc/java", "/etc/java", false},
                          {"/etc/crypto-policies", "/etc/crypto-policies", false},
                          {"/etc/pki", "/etc/pki", false},
                          {"/etc/java-11-openjdk", "/etc/java-11-openjdk", false},
                          {"/etc/java-17-openjdk", "/etc/java-17-openjdk", false},
                          {"/etc/java-21-openjdk", "/etc/java-21-openjdk", false}};
        c.compile_limits = kCompileLimits;
        cfgs.push_back(c);
    }
    {   // Go
        LanguageRuntimeConfig c;
        c.lang = Language::GO;
        c.name = "go";
        c.extensions = {".go"};
        c.needs_compilation = true;
        c.source_name = "submission.go";
        c.compiler_path = resolve_tool({"go"});
        c.compile_args = {"build", "-o", "solution", "submission.go"};
        c.artifact_name = "solution";
        c.compile_env = {"GO111MODULE=off", "GOPATH=/tmp/go",
                         "GOCACHE=/tmp/gocache", "CGO_ENABLED=0"};
        c.seccomp_profile = SeccompProfile::Standard;
        c.extra_mounts = {{"/usr/lib/go", "/usr/lib/go", false}};
        // 若 go 位于非基础目录（如 /usr/local/go/bin），补挂其工具链根（GOROOT）。
        for (auto& m : toolchain_mounts(c.compiler_path)) c.extra_mounts.push_back(m);
        c.compile_limits = kCompileLimits;
        c.compile_limits.cpu_time_ms = 60000;
        c.compile_limits.wall_time_ms = 120000;
        cfgs.push_back(c);
    }
    {   // Rust
        LanguageRuntimeConfig c;
        c.lang = Language::RUST;
        c.name = "rust";
        c.extensions = {".rs"};
        c.needs_compilation = true;
        c.source_name = "submission.rs";
        c.compiler_path = resolve_tool({"rustc"});
        c.compile_args = {"-O", "-o", "solution", "submission.rs"};
        c.artifact_name = "solution";
        c.seccomp_profile = SeccompProfile::Standard;
        c.extra_mounts = {{"/usr/lib/rustlib", "/usr/lib/rustlib", false}};
        // rustup 安装下 rustc 及其 std 库在 ~/.rustup/toolchains/<tc>/{bin,lib}；
        // resolve_tool 已 realpath 到该真实路径，补挂工具链根即覆盖 bin+lib。
        for (auto& m : toolchain_mounts(c.compiler_path)) c.extra_mounts.push_back(m);
        c.compile_limits = kCompileLimits;
        cfgs.push_back(c);
    }
    return cfgs;
}

const std::vector<LanguageRuntimeConfig>& configs() {
    static const std::vector<LanguageRuntimeConfig> cfgs = build_configs();
    return cfgs;
}

} // namespace

std::string resolve_tool(const std::vector<std::string>& candidates) {
    auto is_exec = [](const std::string& p) {
        return !p.empty() && access(p.c_str(), X_OK) == 0;
    };
    auto canonical_exec = [&](const std::string& p) {
        char resolved[PATH_MAX];
        if (realpath(p.c_str(), resolved) != nullptr) {
            return std::string(resolved);
        }
        return p;
    };

    std::vector<std::string> dirs;
    if (const char* path = std::getenv("PATH")) {
        std::stringstream ss(path);
        std::string d;
        while (std::getline(ss, d, ':')) {
            if (!d.empty()) dirs.push_back(d);
        }
    }
    for (const char* d : {"/usr/bin", "/bin", "/usr/local/bin",
                          "/opt/homebrew/bin", "/usr/local/go/bin"}) {
        dirs.push_back(d);
    }
    if (const char* home = std::getenv("HOME")) {
        dirs.push_back(std::string(home) + "/.cargo/bin");
    }

    for (const auto& cand : candidates) {
        if (cand.find('/') != std::string::npos) {
            if (is_exec(cand)) return canonical_exec(cand);
            continue;
        }
        for (const auto& dir : dirs) {
            std::string full = dir + "/" + cand;
            if (is_exec(full)) return canonical_exec(full);
        }
    }
    return "";
}

Language LanguageManager::detect_from_extension(const std::string& filename) {
    auto dot = filename.rfind('.');
    if (dot == std::string::npos) return Language::UNKNOWN;
    std::string ext = to_lower(filename.substr(dot));
    for (const auto& c : configs()) {
        for (const auto& e : c.extensions) {
            if (ext == e) return c.lang;
        }
    }
    return Language::UNKNOWN;
}

Language LanguageManager::parse(const std::string& name) {
    return language_from_string(name);
}

const LanguageRuntimeConfig& LanguageManager::get_runtime(Language lang) {
    for (const auto& c : configs()) {
        if (c.lang == lang) return c;
    }
    return configs().front();  // 不应到达
}

const std::vector<Language>& LanguageManager::supported_languages() {
    static const std::vector<Language> langs = [] {
        std::vector<Language> v;
        for (const auto& c : configs()) v.push_back(c.lang);
        return v;
    }();
    return langs;
}

} // namespace cppjudge

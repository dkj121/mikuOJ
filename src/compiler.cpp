#include "cppjudge/compiler.h"

#include "cppjudge/language.h"
#include "cppjudge/log.h"

#include <sys/stat.h>

#include <fstream>
#include <sstream>

namespace cppjudge {

namespace {

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool copy_file(const std::string& from, const std::string& to, std::string& err) {
    std::ifstream src(from, std::ios::binary);
    if (!src.is_open()) { err = "cannot open source file: " + from; return false; }
    std::ofstream dst(to, std::ios::binary);
    if (!dst.is_open()) { err = "cannot write to work dir: " + to; return false; }
    dst << src.rdbuf();
    return dst.good();
}

bool file_exists(const std::string& path) {
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

} // namespace

CompileResult Compiler::compile(SandboxBackend& backend,
                                const std::string& source_file,
                                Language lang,
                                const std::string& work_dir) {
    CompileResult r;
    const LanguageRuntimeConfig& rt = LanguageManager::get_runtime(lang);

    // 1. 复制源码到 work_dir/<source_name>
    const std::string dest = work_dir + "/" + rt.source_name;
    std::string copy_err;
    if (!copy_file(source_file, dest, copy_err)) {
        r.verdict = Verdict::SE;
        r.error_detail = copy_err;
        return r;
    }

    // 2. 解释型语言：无需编译
    if (!rt.needs_compilation) {
        if (rt.interpreter_path.empty()) {
            r.verdict = Verdict::SE;
            r.error_detail = "interpreter not found for language '" + rt.name + "'";
            return r;
        }
        r.success = true;
        r.exec_path = rt.interpreter_path;
        r.exec_args = rt.run_args;
        return r;
    }

    // 3. 编译型语言：在沙箱内编译
    if (rt.compiler_path.empty()) {
        r.verdict = Verdict::SE;
        r.error_detail = "compiler not found for language '" + rt.name + "'";
        return r;
    }

    SandboxRequest req;
    req.executable = rt.compiler_path;
    req.argv = rt.compile_args;
    req.work_dir = work_dir;
    req.lang = rt.name;
    req.is_compile = true;
    req.limits = rt.compile_limits;
    req.envp = rt.compile_env;
    req.extra_mounts = rt.extra_mounts;
    req.stdout_path = work_dir + "/compile_stdout.txt";
    req.stderr_path = work_dir + "/compile_stderr.txt";

    SandboxResult sr = backend.execute(req);
    r.output = read_file(req.stdout_path) + read_file(req.stderr_path);
    r.exit_code = sr.exit_code;

    if (sr.verdict == Verdict::SE) {
        r.verdict = Verdict::SE;
        r.error_detail = sr.error_detail;
        return r;
    }

    const std::string artifact = work_dir + "/" + rt.artifact_name;
    const bool produced = file_exists(artifact);
    if (sr.verdict == Verdict::AC && sr.exit_code == 0 && produced) {
        r.success = true;
        if (rt.interpreter_path.empty()) {
            r.exec_path = "./" + rt.artifact_name;  // native，cwd=work_dir
            r.exec_args = {};
        } else {
            r.exec_path = rt.interpreter_path;       // 如 java
            r.exec_args = rt.run_args;
        }
        spdlog::debug("compiled {} -> {}", rt.name, rt.artifact_name);
    } else {
        r.success = false;
        r.verdict = Verdict::CE;
        if (r.output.empty()) {
            r.error_detail =
                "compiler did not produce artifact '" + rt.artifact_name +
                "'; sandbox verdict=" + verdict_to_string(sr.verdict) +
                ", exit_code=" + std::to_string(sr.exit_code) +
                ", signal=" + std::to_string(sr.signal_num);
        }
    }
    return r;
}

} // namespace cppjudge

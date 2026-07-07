#include "cppjudge/common.h"
#include "cppjudge/comparator.h"
#include "cppjudge/compiler.h"
#include "cppjudge/doctor.h"
#include "cppjudge/language.h"
#include "cppjudge/log.h"
#include "cppjudge/logger.h"
#include "cppjudge/problem.h"
#include "cppjudge/sandbox.h"

#include <getopt.h>
#include <sys/stat.h>

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

using namespace cppjudge;

constexpr const char* kVersion = "0.1.0-dev";

// 判决优先级（高→低）。SV 高于 RE/WA（修 D4）。
int verdict_rank(Verdict v) {
    switch (v) {
        case Verdict::SE:  return 100;
        case Verdict::CE:  return 90;
        case Verdict::TLE: return 80;
        case Verdict::MLE: return 70;
        case Verdict::OLE: return 60;
        case Verdict::SV:  return 50;
        case Verdict::RE:  return 40;
        case Verdict::WA:  return 30;
        case Verdict::AC:  return 0;
    }
    return -1;
}

Verdict merge_verdict(Verdict a, Verdict b) {
    return verdict_rank(a) >= verdict_rank(b) ? a : b;
}

std::string read_file(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// 向 stdout 打印一条结构化最终结果（stdout 专供结果，见全局约束）。
void emit_result(Verdict v, int test_count = -1) {
    std::cout << "{\"final_verdict\":\"" << Logger::json_escape(verdict_to_string(v))
              << "\"";
    if (test_count >= 0) std::cout << ",\"test_count\":" << test_count;
    std::cout << "}\n";
}

void print_usage(const char* prog) {
    std::cerr
        << "Usage: " << prog << " <command> [options]\n\n"
        << "Commands:\n"
        << "  judge     Judge a submission\n"
        << "  doctor    Check environment readiness\n"
        << "  version   Print version\n"
        << "  help      Show this help\n\n"
        << "Judge options:\n"
        << "  --problem=<dir>            Problem directory (required)\n"
        << "  --submission=<file>        Submission file (required)\n"
        << "  --lang=<lang>              Language override (cpp/c/python3/java/go/rust)\n"
        << "  --time-limit-ms=<N>        CPU time limit override\n"
        << "  --memory-limit-mb=<N>      Memory limit override\n"
        << "  --output-limit-mb=<N>      Output size limit override\n"
        << "  --compile-time-limit-ms=<N>\n"
        << "  --compare-mode=<mode>      'exact' or 'floating'\n"
        << "  --sandbox-type=<type>      'auto' | 'builtin' | 'linux-ns'\n"
        << "  --verbose                  Verbose diagnostics\n";
}

struct JudgeArgs {
    std::string problem_dir;
    std::string submission_file;
    std::string lang_override;
    std::string compare_mode;
    std::string sandbox_type;
    std::optional<uint64_t> cpu_time_ms;
    std::optional<uint64_t> memory_mb;
    std::optional<uint64_t> output_mb;
    std::optional<uint64_t> compile_ms;
    bool verbose = false;
};

int run_judge(int argc, char* argv[]) {
    JudgeArgs a;

    static struct option long_opts[] = {
        {"problem",                required_argument, nullptr, 'p'},
        {"submission",             required_argument, nullptr, 's'},
        {"lang",                   required_argument, nullptr, 'l'},
        {"time-limit-ms",          required_argument, nullptr, 't'},
        {"memory-limit-mb",        required_argument, nullptr, 'm'},
        {"output-limit-mb",        required_argument, nullptr, 'o'},
        {"compile-time-limit-ms",  required_argument, nullptr, 'c'},
        {"compare-mode",           required_argument, nullptr, 'C'},
        {"sandbox-type",           required_argument, nullptr, 'S'},
        {"verbose",                no_argument,       nullptr, 'v'},
        {"help",                   no_argument,       nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "", long_opts, nullptr)) != -1) {
        switch (opt) {
            case 'p': a.problem_dir = optarg; break;
            case 's': a.submission_file = optarg; break;
            case 'l': a.lang_override = optarg; break;
            case 't': a.cpu_time_ms = std::stoull(optarg); break;
            case 'm': a.memory_mb = std::stoull(optarg); break;
            case 'o': a.output_mb = std::stoull(optarg); break;
            case 'c': a.compile_ms = std::stoull(optarg); break;
            case 'C': a.compare_mode = optarg; break;
            case 'S': a.sandbox_type = optarg; break;
            case 'v': a.verbose = true; break;
            case 'h': print_usage("cppjudge"); return 0;
            default:  print_usage("cppjudge"); return 2;
        }
    }

    log::init(a.verbose);

    if (a.problem_dir.empty() || a.submission_file.empty()) {
        spdlog::error("--problem and --submission are required");
        emit_result(Verdict::SE);
        return 3;
    }

    // 语言检测：CLI 覆盖优先，否则按扩展名
    Language lang;
    if (!a.lang_override.empty()) {
        lang = LanguageManager::parse(a.lang_override);
        if (lang == Language::UNKNOWN) {
            spdlog::error("unsupported language '{}'", a.lang_override);
            emit_result(Verdict::SE);
            return 3;
        }
    } else {
        lang = LanguageManager::detect_from_extension(a.submission_file);
        if (lang == Language::UNKNOWN) {
            spdlog::error("cannot detect language from '{}'; use --lang", a.submission_file);
            emit_result(Verdict::SE);
            return 3;
        }
    }
    spdlog::info("language: {}", language_to_string(lang));
    const LanguageRuntimeConfig& rt = LanguageManager::get_runtime(lang);

    // 加载题目
    std::string error;
    auto problem = ProblemManager::load(a.problem_dir, error);
    if (!problem || !ProblemManager::validate(*problem, error)) {
        spdlog::error("cannot load problem: {}", error);
        emit_result(Verdict::SE);   // 修 D12：SE 也要输出到 stdout
        return 3;
    }

    // 解析限制：以题目为基线，CLI 显式覆盖（修 D10）
    Limits limits = problem->limits;
    if (a.cpu_time_ms) {
        limits.cpu_time_ms   = *a.cpu_time_ms;
        limits.wall_time_ms  = ProblemManager::safe_wall_time(limits.cpu_time_ms);
    }
    if (a.memory_mb)   limits.memory_mb     = *a.memory_mb;
    if (a.output_mb)   limits.output_size_mb = *a.output_mb;
    if (a.compile_ms)  limits.compile_time_ms = *a.compile_ms;

    std::string compare_mode =
        !a.compare_mode.empty() ? a.compare_mode : problem->compare_mode;
    std::string sandbox_type =
        !a.sandbox_type.empty() ? a.sandbox_type : problem->sandbox_type;
    if (sandbox_type.empty()) sandbox_type = "auto";

    // 选择沙箱后端（真正按类型分发，修 D11）
    auto backend = make_sandbox(sandbox_type, error);
    if (!backend) {
        spdlog::error("sandbox backend unavailable: {}", error);
        emit_result(Verdict::SE);
        return 3;
    }

    // 生产模式 fail-closed：不安全后端拒绝运行
    const char* env = std::getenv("CPPJUDGE_ENV");
    const char* prod = std::getenv("CPPJUDGE_PRODUCTION");
    const bool is_prod = (env && std::string(env) == "production") ||
                         (prod && std::string(prod) == "1");
    if (is_prod && !backend->is_secure()) {
        spdlog::error("insecure sandbox '{}' rejected in production mode", backend->name());
        emit_result(Verdict::SE);
        return 3;
    }
    spdlog::info("sandbox backend: {} (secure={})", backend->name(), backend->is_secure());

    // 运行目录
    std::string run_dir = Logger::create_run_dir("build");

    // 编译（或复制源码）
    CompileResult comp = Compiler::compile(*backend, a.submission_file, lang, run_dir);
    if (comp.verdict == Verdict::SE) {
        spdlog::error("compile stage system error: {}", comp.error_detail);
        RunResult rr;
        rr.verdict = Verdict::SE;
        rr.error_detail = comp.error_detail;
        rr.run_id = run_dir;
        rr.run_dir = run_dir;
        Logger::write_log("build/judge_log.json", a.problem_dir, a.submission_file,
                          Verdict::SE, {rr});
        emit_result(Verdict::SE);
        return 3;
    }
    if (!comp.success) {
        spdlog::info("compile error");
        RunResult rr;
        rr.verdict = Verdict::CE;
        rr.error_detail = comp.output;
        rr.run_id = run_dir;
        rr.run_dir = run_dir;
        Logger::write_log("build/judge_log.json", a.problem_dir, a.submission_file,
                          Verdict::CE, {rr});
        emit_result(Verdict::CE, 0);
        return 1;
    }

    // 逐测试点执行
    std::vector<RunResult> results;
    Verdict final_verdict = Verdict::AC;

    for (const auto& tc : problem->test_cases) {
        RunResult tr;
        tr.test_index = tc.index;
        tr.run_id = run_dir;
        tr.run_dir = run_dir;

        std::string test_dir = run_dir + "/test_" + std::to_string(tc.index);
        mkdir(test_dir.c_str(), 0755);

        SandboxRequest req;
        req.executable = comp.exec_path;
        req.argv = comp.exec_args;
        req.work_dir = run_dir;
        req.lang = rt.name;
        req.limits = limits;
        req.stdin_path = tc.input_file;
        req.stdout_path = test_dir + "/stdout.txt";
        req.stderr_path = test_dir + "/stderr.txt";
        req.extra_mounts = rt.extra_mounts;

        SandboxResult sr = backend->execute(req);
        tr.exit_code = sr.exit_code;
        tr.signal_num = sr.signal_num;
        tr.time_ms = sr.time_ms;
        tr.wall_time_ms = sr.wall_time_ms;
        tr.memory_kb = sr.memory_kb;
        tr.output_truncated = sr.output_truncated;

        if (sr.verdict != Verdict::AC) {
            tr.verdict = sr.verdict;
            tr.error_detail = sr.error_detail;
        } else {
            std::string user_out = read_file(req.stdout_path);
            std::string expected = read_file(tc.output_file);
            CompareResult cr = (compare_mode == "floating")
                ? Comparator::compare_floating(user_out, expected,
                                               problem->float_abs_eps,
                                               problem->float_rel_eps)
                : Comparator::compare_exact(user_out, expected);
            if (cr.is_match) {
                tr.verdict = Verdict::AC;
            } else {
                tr.verdict = Verdict::WA;
                tr.compare_detail = cr.detail;
            }
        }

        spdlog::info("test {}: {} ({}ms, {}KB)", tc.index,
                     verdict_to_string(tr.verdict), tr.time_ms, tr.memory_kb);
        final_verdict = merge_verdict(final_verdict, tr.verdict);
        results.push_back(tr);
    }

    Logger::write_log("build/judge_log.json", a.problem_dir, a.submission_file,
                      final_verdict, results);
    emit_result(final_verdict, static_cast<int>(results.size()));
    return final_verdict == Verdict::AC ? 0 : 1;
}

} // namespace

int main(int argc, char* argv[]) {
    cppjudge::log::install_crash_handler();

    if (argc < 2) {
        print_usage(argv[0]);
        return 2;
    }
    std::string cmd = argv[1];
    if (cmd == "judge")   return run_judge(argc - 1, argv + 1);
    if (cmd == "doctor")  { cppjudge::log::init(false); return cppjudge::Doctor::check() ? 0 : 2; }
    if (cmd == "version" || cmd == "--version") {
        std::cout << "cppjudge " << kVersion << "\n";
        return 0;
    }
    if (cmd == "help" || cmd == "--help") {
        print_usage(argv[0]);
        return 0;
    }
    std::cerr << "Unknown command: " << cmd << "\n";
    print_usage(argv[0]);
    return 2;
}

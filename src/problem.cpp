#include "cppjudge/problem.h"

#include <dirent.h>
#include <sys/stat.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <set>

namespace cppjudge {

namespace {

// 各项资源限制的上界，validate() 中强制校验。
constexpr uint64_t kMaxCpuTimeMs      = 60'000;
constexpr uint64_t kMaxWallTimeMs     = 180'000;
constexpr uint64_t kMaxMemoryMb       = 16 * 1024;
constexpr uint64_t kMaxOutputMb       = 1024;
constexpr uint64_t kMaxCompileTimeMs  = 120'000;

const std::set<std::string> kValidSandboxTypes = {"auto", "builtin", "linux-ns"};

// 读取 YAML 键值，缺失时返回默认值；类型不匹配时抛出并报告键名。
template <typename T>
T get_or(const YAML::Node& root, const char* key, T fallback) {
    if (!root[key]) return fallback;
    try {
        return root[key].as<T>();
    } catch (const YAML::BadConversion&) {
        throw YAML::Exception(
            YAML::Mark::null_mark(),
            std::string("bad type for key '") + key + "' in problem.json");
    }
}

// 从文件名 stem 提取测试点序号（严格模式）。
// 拒绝空串、前导零、非数字字符、部分解析、溢出 → 返回 -1。
int parse_test_index(const std::string& stem) {
    if (stem.empty()) return -1;
    if (stem.size() > 1 && stem[0] == '0') return -1;  // 拒绝前导零
    for (char c : stem) {
        if (c < '0' || c > '9') return -1;
    }
    try {
        std::size_t pos = 0;
        int val = std::stoi(stem, &pos);
        if (pos != stem.size()) return -1;
        return val;
    } catch (...) {
        return -1;
    }
}

}  // namespace

uint64_t ProblemManager::safe_wall_time(uint64_t cpu_time_ms) {
    if (cpu_time_ms == 0) return 0;
    if (cpu_time_ms > kMaxWallTimeMs / 3) return kMaxWallTimeMs;
    uint64_t wall = cpu_time_ms * 3;
    if (wall > kMaxWallTimeMs) wall = kMaxWallTimeMs;
    return wall;
}

[[nodiscard]] std::unique_ptr<Problem> ProblemManager::load(
    const std::string& problem_dir, std::string& error) {

    // 规范化路径，去掉末尾 '/'
    std::string dir = problem_dir;
    while (!dir.empty() && dir.back() == '/') dir.pop_back();

    const std::string json_path = dir + "/problem.json";
    {
        std::ifstream probe(json_path);
        if (!probe.is_open()) {
            error = "cannot open " + json_path;
            return nullptr;
        }
    }

    auto problem = std::make_unique<Problem>();
    problem->problem_dir = dir;

    try {
        YAML::Node root = YAML::LoadFile(json_path);
        problem->title                  = get_or<std::string>(root, "title", "");
        problem->limits.cpu_time_ms     = get_or<uint64_t>(root, "time_limit_ms", 2000);
        problem->limits.memory_mb       = get_or<uint64_t>(root, "memory_limit_mb", 256);
        problem->limits.output_size_mb  = get_or<uint64_t>(root, "output_limit_mb", 10);
        problem->limits.compile_time_ms = get_or<uint64_t>(root, "compile_time_limit_ms", 5000);
        problem->limits.stack_mb        = get_or<uint64_t>(root, "stack_limit_mb", 8);
        problem->limits.max_processes   = get_or<uint32_t>(root, "max_processes", 64);
        problem->compare_mode           = get_or<std::string>(root, "compare_mode", "exact");
        problem->sandbox_type           = get_or<std::string>(root, "sandbox_type", "auto");
        problem->float_abs_eps          = get_or<double>(root, "float_abs_eps", 1e-9);
        problem->float_rel_eps          = get_or<double>(root, "float_rel_eps", 1e-6);

        // 墙上时间：JSON 显式设置优先，否则 CPU×3
        if (root["wall_time_ms"]) {
            problem->limits.wall_time_ms = get_or<uint64_t>(root, "wall_time_ms", 0);
        } else {
            problem->limits.wall_time_ms = safe_wall_time(
                problem->limits.cpu_time_ms);
        }
    } catch (const YAML::Exception& e) {
        error = "problem.json parse error: " + std::string(e.what());
        return nullptr;
    }

    // 扫描 input/ 目录，收集 .in 文件
    const std::string input_dir = dir + "/input";
    DIR* dp = opendir(input_dir.c_str());
    if (dp == nullptr) {
        error = "cannot open input directory: " + input_dir;
        return nullptr;
    }
    std::vector<std::string> input_files;
    for (struct dirent* entry; (entry = readdir(dp)) != nullptr;) {
        std::string name(entry->d_name);
        if (name == "." || name == "..") continue;
        if (entry->d_type == DT_DIR) continue;
        if (name.size() >= 3 && name.compare(name.size() - 3, 3, ".in") == 0) {
            input_files.push_back(name);
        }
    }
    closedir(dp);
    std::sort(input_files.begin(), input_files.end());

    // 将每个 .in 与对应的 .out 配对
    for (const auto& inf : input_files) {
        const std::string stem = inf.substr(0, inf.size() - 3);
        const std::string out_path = dir + "/output/" + stem + ".out";

        struct stat st{};
        if (stat(out_path.c_str(), &st) != 0) {
            error = "missing expected output file: " + out_path;
            return nullptr;
        }
        if (!S_ISREG(st.st_mode)) {
            error = "output path is not a regular file: " + out_path;
            return nullptr;
        }

        Problem::TestCase tc;
        tc.input_file = input_dir + "/" + inf;
        tc.output_file = out_path;

        int parsed = parse_test_index(stem);
        if (parsed > 0) {
            tc.index = parsed;
        } else {
            tc.index = static_cast<int>(problem->test_cases.size()) + 1;
        }
        problem->test_cases.push_back(tc);
    }

    if (problem->test_cases.empty()) {
        error = "no test cases (.in files) found in " + input_dir;
        return nullptr;
    }
    return problem;
}

[[nodiscard]] bool ProblemManager::validate(const Problem& problem,
                                            std::string& error) {
    if (problem.title.empty()) {
        error = "title is empty"; return false;
    }

    // 时间限制
    if (problem.limits.cpu_time_ms == 0) {
        error = "time_limit_ms is 0"; return false;
    }
    if (problem.limits.cpu_time_ms > kMaxCpuTimeMs) {
        error = "time_limit_ms exceeds maximum (" +
                std::to_string(kMaxCpuTimeMs) + ")"; return false;
    }

    // 内存限制
    if (problem.limits.memory_mb == 0) {
        error = "memory_limit_mb is 0"; return false;
    }
    if (problem.limits.memory_mb > kMaxMemoryMb) {
        error = "memory_limit_mb exceeds maximum (" +
                std::to_string(kMaxMemoryMb) + ")"; return false;
    }

    // 输出限制
    if (problem.limits.output_size_mb == 0) {
        error = "output_limit_mb is 0"; return false;
    }
    if (problem.limits.output_size_mb > kMaxOutputMb) {
        error = "output_limit_mb exceeds maximum (" +
                std::to_string(kMaxOutputMb) + ")"; return false;
    }

    // 编译时间限制
    if (problem.limits.compile_time_ms > kMaxCompileTimeMs) {
        error = "compile_time_limit_ms exceeds maximum (" +
                std::to_string(kMaxCompileTimeMs) + ")"; return false;
    }

    // 比较模式
    if (problem.compare_mode != "exact" && problem.compare_mode != "floating") {
        error = "compare_mode must be 'exact' or 'floating'";
        return false;
    }

    // 沙箱类型
    if (kValidSandboxTypes.find(problem.sandbox_type) == kValidSandboxTypes.end()) {
        error = "sandbox_type must be one of: auto, builtin, linux-ns";
        return false;
    }

    // 浮点容差不允许负值
    if (problem.float_abs_eps < 0.0) {
        error = "float_abs_eps must be >= 0";
        return false;
    }
    if (problem.float_rel_eps < 0.0) {
        error = "float_rel_eps must be >= 0";
        return false;
    }

    // 栈与进程数
    if (problem.limits.stack_mb == 0) {
        error = "stack_limit_mb is 0"; return false;
    }
    if (problem.limits.stack_mb > 4096) {
        error = "stack_limit_mb exceeds maximum (4096)"; return false;
    }
    if (problem.limits.max_processes == 0) {
        error = "max_processes is 0"; return false;
    }
    if (problem.limits.max_processes > 1024) {
        error = "max_processes exceeds maximum (1024)"; return false;
    }

    // 墙上时间：必须 ≥ CPU 时间，不允许零值绕过
    if (problem.limits.wall_time_ms > kMaxWallTimeMs) {
        error = "wall_time_ms exceeds maximum (" +
                std::to_string(kMaxWallTimeMs) + ")"; return false;
    }
    if (problem.limits.cpu_time_ms > 0 && problem.limits.wall_time_ms == 0) {
        error = "wall_time_ms is 0 (must be >= time_limit_ms)";
        return false;
    }
    if (problem.limits.wall_time_ms > 0 &&
        problem.limits.wall_time_ms < problem.limits.cpu_time_ms) {
        error = "wall_time_ms must be >= time_limit_ms";
        return false;
    }

    if (problem.test_cases.empty()) {
        error = "no test cases"; return false;
    }
    return true;
}

}  // namespace cppjudge

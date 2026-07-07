#include "cppjudge/problem.h"

#include <dirent.h>
#include <sys/stat.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>

namespace cppjudge {

namespace {
// yaml-cpp 读带默认值的标量
template <typename T>
T get_or(const YAML::Node& root, const char* key, T fallback) {
    return root[key] ? root[key].as<T>() : fallback;
}
} // namespace

std::unique_ptr<Problem> ProblemManager::load(const std::string& problem_dir,
                                              std::string& error) {
    const std::string json_path = problem_dir + "/problem.json";
    {
        std::ifstream probe(json_path);
        if (!probe.is_open()) {
            error = "cannot open " + json_path;
            return nullptr;
        }
    }

    auto problem = std::make_unique<Problem>();
    problem->problem_dir = problem_dir;

    try {
        // JSON 是 YAML 的子集，用 yaml-cpp 直接解析。
        YAML::Node root = YAML::LoadFile(json_path);
        problem->title                  = get_or<std::string>(root, "title", "");
        problem->limits.cpu_time_ms     = get_or<uint64_t>(root, "time_limit_ms", 2000);
        problem->limits.memory_mb       = get_or<uint64_t>(root, "memory_limit_mb", 256);
        problem->limits.output_size_mb  = get_or<uint64_t>(root, "output_limit_mb", 10);
        problem->limits.compile_time_ms = get_or<uint64_t>(root, "compile_time_limit_ms", 5000);
        problem->compare_mode           = get_or<std::string>(root, "compare_mode", "exact");
        problem->sandbox_type           = get_or<std::string>(root, "sandbox_type", "auto");
        problem->float_abs_eps          = get_or<double>(root, "float_abs_eps", 1e-9);
        problem->float_rel_eps          = get_or<double>(root, "float_rel_eps", 1e-6);
        problem->limits.wall_time_ms    = problem->limits.cpu_time_ms * 3;  // 墙上 = CPU × 3
    } catch (const YAML::Exception& e) {
        error = "problem.json parse error: " + std::string(e.what());
        return nullptr;
    }

    // 扫描 input/*.in
    const std::string input_dir = problem_dir + "/input";
    DIR* dp = opendir(input_dir.c_str());
    if (dp == nullptr) {
        error = "cannot open input directory: " + input_dir;
        return nullptr;
    }
    std::vector<std::string> input_files;
    for (struct dirent* entry; (entry = readdir(dp)) != nullptr;) {
        std::string name(entry->d_name);
        if (name.size() >= 3 && name.compare(name.size() - 3, 3, ".in") == 0) {
            input_files.push_back(name);
        }
    }
    closedir(dp);
    std::sort(input_files.begin(), input_files.end());

    // 配对 .in → .out
    for (const auto& inf : input_files) {
        const std::string stem = inf.substr(0, inf.size() - 3);
        const std::string out_path = problem_dir + "/output/" + stem + ".out";
        struct stat st{};
        if (stat(out_path.c_str(), &st) != 0) {
            error = "missing expected output file: " + out_path;
            return nullptr;
        }
        Problem::TestCase tc;
        tc.input_file = input_dir + "/" + inf;
        tc.output_file = out_path;
        try {
            tc.index = std::stoi(stem);
        } catch (...) {
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

bool ProblemManager::validate(const Problem& problem, std::string& error) {
    if (problem.title.empty())            { error = "title is empty"; return false; }
    if (problem.limits.cpu_time_ms == 0)  { error = "time_limit_ms is 0"; return false; }
    if (problem.limits.memory_mb == 0)    { error = "memory_limit_mb is 0"; return false; }
    if (problem.compare_mode != "exact" && problem.compare_mode != "floating") {
        error = "compare_mode must be 'exact' or 'floating'";
        return false;
    }
    if (problem.sandbox_type != "auto" &&
        problem.sandbox_type != "linux-ns" &&
        problem.sandbox_type != "nsjail") {
        error = "sandbox_type must be 'auto', 'linux-ns', or 'nsjail'";
        return false;
    }
    if (problem.test_cases.empty())       { error = "no test cases"; return false; }
    return true;
}

} // namespace cppjudge

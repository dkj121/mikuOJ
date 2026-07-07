#include "cppjudge/logger.h"

#include <sys/stat.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>

namespace cppjudge {

std::string Logger::json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    return out;
}

namespace {

std::string generate_run_id() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y%m%d-%H%M%S");

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<unsigned> dis(0, 0xFFFFFF);
    oss << "-" << std::hex << std::setw(6) << std::setfill('0') << dis(gen);
    return oss.str();
}

} // namespace

std::string Logger::create_run_dir(const std::string& base_dir) {
    mkdir(base_dir.c_str(), 0755);
    std::string runs = base_dir + "/runs";
    mkdir(runs.c_str(), 0755);
    std::string run_dir = runs + "/" + generate_run_id();
    mkdir(run_dir.c_str(), 0755);
    return run_dir;
}

bool Logger::write_log(const std::string& output_path,
                       const std::string& problem_dir,
                       const std::string& submission_file,
                       Verdict final_verdict,
                       const std::vector<RunResult>& results) {
    std::ofstream f(output_path);
    if (!f.is_open()) return false;

    auto q = [](const std::string& s) { return "\"" + json_escape(s) + "\""; };

    f << "{\n";
    f << "  \"schema_version\": 1,\n";
    f << "  \"tool\": \"cppjudge\",\n";
    f << "  \"cppjudge_version\": \"0.1.0-dev\",\n";
    f << "  \"problem_dir\": " << q(problem_dir) << ",\n";
    f << "  \"submission_file\": " << q(submission_file) << ",\n";
    f << "  \"final_verdict\": " << q(verdict_to_string(final_verdict)) << ",\n";
    f << "  \"results\": [\n";

    for (size_t i = 0; i < results.size(); ++i) {
        const RunResult& r = results[i];
        f << "    {\n";
        f << "      \"test_index\": " << r.test_index << ",\n";
        f << "      \"verdict\": " << q(verdict_to_string(r.verdict)) << ",\n";
        f << "      \"time_ms\": " << r.time_ms << ",\n";
        f << "      \"wall_time_ms\": " << r.wall_time_ms << ",\n";
        f << "      \"memory_kb\": " << r.memory_kb << ",\n";
        f << "      \"exit_code\": " << r.exit_code << ",\n";
        f << "      \"signal\": " << r.signal_num << ",\n";
        f << "      \"output_truncated\": "
          << (r.output_truncated ? "true" : "false") << ",\n";
        f << "      \"run_id\": " << q(r.run_id) << ",\n";
        f << "      \"run_dir\": " << q(r.run_dir);
        if (!r.compare_detail.empty()) {
            f << ",\n      \"compare_detail\": " << q(r.compare_detail);
        }
        if (!r.error_detail.empty()) {
            f << ",\n      \"error_detail\": " << q(r.error_detail);
        }
        f << "\n    }";
        if (i + 1 < results.size()) f << ",";
        f << "\n";
    }

    f << "  ]\n";
    f << "}\n";
    return f.good();
}

} // namespace cppjudge

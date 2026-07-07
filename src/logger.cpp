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

namespace {

// 校验 s[i..] 是否为合法的 UTF-8 多字节序列（首字节已知为 >= 0x80）。
// 合法则返回该序列长度（2..4），否则返回 0。拒绝过长编码与代理区/越界码点。
int utf8_sequence_len(const std::string& s, size_t i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    int len;
    unsigned int cp;
    if ((c & 0xE0) == 0xC0) { len = 2; cp = c & 0x1F; }
    else if ((c & 0xF0) == 0xE0) { len = 3; cp = c & 0x0F; }
    else if ((c & 0xF8) == 0xF0) { len = 4; cp = c & 0x07; }
    else return 0;  // 0x80..0xBF 独立续接字节，或 0xF8+ 非法首字节
    if (i + static_cast<size_t>(len) > s.size()) return 0;
    for (int k = 1; k < len; ++k) {
        const unsigned char cc = static_cast<unsigned char>(s[i + k]);
        if ((cc & 0xC0) != 0x80) return 0;  // 续接字节必须是 10xxxxxx
        cp = (cp << 6) | (cc & 0x3F);
    }
    // 拒绝过长编码（非最短形式）与非法码点
    static const unsigned int kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
    if (cp < kMin[len]) return 0;
    if (cp > 0x10FFFF) return 0;
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0;  // UTF-16 代理区
    return len;
}

}  // namespace

std::string Logger::json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
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
                } else if (c < 0x80) {
                    out += static_cast<char>(c);  // 可打印 ASCII（含 0x7F DEL，JSON 合法）
                } else {
                    // 非 ASCII：仅当构成合法 UTF-8 序列时原样保留，否则用 U+FFFD 替换，
                    // 保证输出始终是合法 UTF-8（RFC 8259 要求）。
                    int len = utf8_sequence_len(s, i);
                    if (len > 0) {
                        out.append(s, i, static_cast<size_t>(len));
                        i += static_cast<size_t>(len) - 1;
                    } else {
                        out += "\\ufffd";
                    }
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

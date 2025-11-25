#include "utils.h"

#include <chrono>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <atomic>    // <-- add this
#include <cstring> 

#ifdef __linux__
#include <sched.h>
#include <unistd.h>
#endif

namespace {

enum class LogLevel { TRACE = 0, DEBUG, INFO, WARN, ERROR, OFF };

std::atomic<LogLevel> g_level{LogLevel::INFO};
std::mutex g_log_mu;

LogLevel parse_level(const std::string& name) {
    std::string s;
    s.reserve(name.size());
    for (char c : name) s.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if (s == "TRACE") return LogLevel::TRACE;
    if (s == "DEBUG") return LogLevel::DEBUG;
    if (s == "INFO")  return LogLevel::INFO;
    if (s == "WARN" || s == "WARNING") return LogLevel::WARN;
    if (s == "ERROR") return LogLevel::ERROR;
    if (s == "OFF")   return LogLevel::OFF;
    return LogLevel::INFO;
}

const char* level_name(LogLevel lvl) {
    switch (lvl) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::OFF:   return "OFF";
    }
    return "?";
}

void log_impl(LogLevel lvl, const std::string& msg) {
    if (lvl < g_level.load(std::memory_order_relaxed) || lvl == LogLevel::OFF) return;

    using clock = std::chrono::system_clock;
    auto now = clock::now();
    auto t   = clock::to_time_t(now);
    auto tp_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    long ms = static_cast<long>(tp_ms.count() % 1000);

    std::tm tm;
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif

    std::ostringstream oss;
    oss << "[" << std::put_time(&tm, "%Y-%m-%d %H:%M:%S")
        << "." << std::setw(3) << std::setfill('0') << ms << "] "
        << "[" << level_name(lvl) << "] "
        << "[tid " << std::this_thread::get_id() << "] "
        << msg;

    std::lock_guard<std::mutex> lk(g_log_mu);
    std::cerr << oss.str() << "\n";
}

} // namespace

void log_set_level(const std::string& level_name_str) {
    g_level.store(parse_level(level_name_str), std::memory_order_relaxed);
}

void log_trace(const std::string& msg) { log_impl(LogLevel::TRACE, msg); }
void log_debug(const std::string& msg) { log_impl(LogLevel::DEBUG, msg); }
void log_info (const std::string& msg) { log_impl(LogLevel::INFO,  msg); }
void log_warn (const std::string& msg) { log_impl(LogLevel::WARN,  msg); }
void log_error(const std::string& msg) { log_impl(LogLevel::ERROR, msg); }

// URL encode/decode (simple, enough for keys/values)
std::string url_encode(const std::string& in) {
    std::ostringstream oss;
    oss << std::hex << std::uppercase;
    for (unsigned char c : in) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            oss << c;
        } else if (c == ' ') {
            oss << '+';
        } else {
            oss << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
    }
    return oss.str();
}

std::string url_decode(const std::string& in) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '+') {
            oss << ' ';
        } else if (c == '%' && i + 2 < in.size()) {
            int v = 0;
            std::istringstream iss(in.substr(i + 1, 2));
            iss >> std::hex >> v;
            oss << static_cast<char>(v);
            i += 2;
        } else {
            oss << c;
        }
    }
    return oss.str();
}

bool set_process_affinity(const std::string& cpu_spec, std::string* err_msg) {
#ifdef __linux__
    cpu_set_t set;
    CPU_ZERO(&set);

    auto add_cpu = [&](int cpu) {
        if (cpu < 0 || cpu >= CPU_SETSIZE) {
            if (err_msg) *err_msg = "CPU index out of range";
            return false;
        }
        CPU_SET(cpu, &set);
        return true;
    };

    std::string s = cpu_spec;
    std::size_t start = 0;
    while (start < s.size()) {
        std::size_t comma = s.find(',', start);
        std::string part = s.substr(start, comma == std::string::npos ? std::string::npos : (comma - start));

        std::size_t dash = part.find('-');
        if (dash == std::string::npos) {
            int cpu = std::stoi(part);
            if (!add_cpu(cpu)) return false;
        } else {
            int lo = std::stoi(part.substr(0, dash));
            int hi = std::stoi(part.substr(dash + 1));
            if (lo > hi) std::swap(lo, hi);
            for (int c = lo; c <= hi; ++c) {
                if (!add_cpu(c)) return false;
            }
        }

        if (comma == std::string::npos) break;
        start = comma + 1;
    }

    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        if (err_msg) *err_msg = "sched_setaffinity failed: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
#else
    (void)cpu_spec;
    if (err_msg) *err_msg = "CPU affinity not supported on this platform";
    return false;
#endif
}

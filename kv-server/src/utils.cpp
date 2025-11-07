// src/utils.cpp
#include "utils.h"

#include <atomic>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

namespace {

enum class Level { TRACE=0, DEBUG, INFO, WARN, ERROR, OFF };

std::atomic<Level> g_level{Level::INFO};
std::mutex g_log_mu;

inline Level parse_level(const std::string& s) {
    std::string t;
    t.reserve(s.size());
    for (char c : s) t.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    if (t == "TRACE") return Level::TRACE;
    if (t == "DEBUG") return Level::DEBUG;
    if (t == "INFO")  return Level::INFO;
    if (t == "WARN" || t == "WARNING")  return Level::WARN;
    if (t == "ERROR") return Level::ERROR;
    if (t == "OFF" || t == "NONE") return Level::OFF;
    return Level::INFO;
}

inline const char* level_name(Level lv) {
    switch (lv) {
        case Level::TRACE: return "TRACE";
        case Level::DEBUG: return "DEBUG";
        case Level::INFO:  return "INFO";
        case Level::WARN:  return "WARN";
        case Level::ERROR: return "ERROR";
        case Level::OFF:   return "OFF";
    }
    return "INFO";
}

// Simple ANSI colors (can be disabled by setting NO_COLOR env if desired)
inline const char* level_color(Level lv) {
    switch (lv) {
        case Level::TRACE: return "\x1b[90m"; // gray
        case Level::DEBUG: return "\x1b[36m"; // cyan
        case Level::INFO:  return "\x1b[32m"; // green
        case Level::WARN:  return "\x1b[33m"; // yellow
        case Level::ERROR: return "\x1b[31m"; // red
        case Level::OFF:   return "\x1b[0m";
    }
    return "\x1b[0m";
}

inline std::string timestamp_now() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t   = system_clock::to_time_t(now);
    const auto ms  = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;

    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
        << '.' << std::setw(3) << std::setfill('0') << ms.count();
    return oss.str();
}

inline void log_impl(Level lv, const std::string& msg) {
    if (lv < g_level.load()) return;

    std::ostringstream line;
    line << '[' << timestamp_now() << "] "
         << level_name(lv) << " "
         << "(tid:" << std::this_thread::get_id() << ") "
         << msg;

    const bool use_color =
        std::getenv("NO_COLOR") == nullptr && std::getenv("CLICOLOR") != nullptr;

    std::lock_guard<std::mutex> lk(g_log_mu);
    std::ostream& out = (lv == Level::ERROR) ? std::cerr : std::clog;

    if (use_color) {
        out << level_color(lv) << line.str() << "\x1b[0m" << std::endl;
    } else {
        out << line.str() << std::endl;
    }
}

} // namespace

// -------- Public API from utils.h --------------------------------------

void log_set_level(const std::string& level_name_str) {
    g_level.store(parse_level(level_name_str));
}

void log_trace(const std::string& msg) { log_impl(Level::TRACE, msg); }
void log_debug(const std::string& msg) { log_impl(Level::DEBUG, msg); }
void log_info (const std::string& msg) { log_impl(Level::INFO,  msg); }
void log_warn (const std::string& msg) { log_impl(Level::WARN,  msg); }
void log_error(const std::string& msg) { log_impl(Level::ERROR, msg); }

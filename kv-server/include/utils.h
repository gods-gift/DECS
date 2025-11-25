#pragma once
#include <string>

/**
 * Logging API (thread-safe).
 * Levels: TRACE < DEBUG < INFO < WARN < ERROR < OFF
 */
void log_set_level(const std::string& level_name);

void log_trace(const std::string& msg);
void log_debug(const std::string& msg);
void log_info (const std::string& msg);
void log_warn (const std::string& msg);
void log_error(const std::string& msg);

/** URL encode / decode helpers for path and query values. */
std::string url_encode(const std::string& in);
std::string url_decode(const std::string& in);

/**
 * Optional: set current process CPU affinity.
 * cpu_spec like "0-1", "2,3", "0-3,5".
 * Returns true on success, false on failure (err_msg optional).
 */
bool set_process_affinity(const std::string& cpu_spec, std::string* err_msg = nullptr);

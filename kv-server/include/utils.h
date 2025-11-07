// include/utils.h
#pragma once
#include <string>

/**
 * Set global log level.
 * Accepted (case-insensitive): TRACE, DEBUG, INFO, WARN, ERROR, OFF
 */
void log_set_level(const std::string& level_name);

/** Logging helpers (thread-safe). */
void log_trace(const std::string& msg);
void log_debug(const std::string& msg);
void log_info (const std::string& msg);
void log_warn (const std::string& msg);
void log_error(const std::string& msg);

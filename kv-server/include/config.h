// include/config.h
#pragma once
#include <string>

/**
 * Global server configuration.
 *
 * Sources (in order of precedence):
 *  1) CLI overrides (parsed in main.cpp)
 *  2) JSON file (KV_SERVER_CONFIG env var, then config/server_config.json, then ./server_config.json)
 *  3) Compile-time defaults: DEFAULT_SERVER_PORT, DEFAULT_CACHE_CAPACITY
 *  4) Sensible runtime defaults (e.g., thread_pool_size ~= hardware_concurrency)
 */
struct Config {
    int         server_port      = 8080;         // HTTP port
    int         cache_size       = 100;          // LRU capacity (items)
    int         thread_pool_size = 8;            // worker threads
    std::string database_path    = "kv_store.db";// SQLite path (or ignored for other DBs)
    std::string log_level        = "INFO";       // TRACE|DEBUG|INFO|WARN|ERROR|OFF
};

/**
 * Load effective configuration by reading JSON (if present),
 * applying defaults, and validating/clamping fields.
 *
 * The loader looks for:
 *  - KV_SERVER_CONFIG (env var path)
 *  - config/server_config.json
 *  - ./server_config.json
 *
 * See config.cpp for details.
 */
Config load_config();

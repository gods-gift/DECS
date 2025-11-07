// src/config.cpp
#include "config.h"
#include "utils.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <thread>
#include <algorithm>

#include <nlohmann/json.hpp>

#ifndef DEFAULT_SERVER_PORT
#define DEFAULT_SERVER_PORT 8080
#endif

#ifndef DEFAULT_CACHE_CAPACITY
#define DEFAULT_CACHE_CAPACITY 100
#endif

using json = nlohmann::json;

namespace {

std::string read_file(const std::string& path) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs) return {};
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

bool file_exists(const std::string& path) {
    std::ifstream ifs(path);
    return static_cast<bool>(ifs);
}

void apply_json(Config& cfg, const json& j, const std::string& source) {
    try {
        if (j.contains("server_port") && j["server_port"].is_number_integer()) {
            int v = j["server_port"].get<int>();
            if (v > 0 && v < 65536) cfg.server_port = v;
            else log_warn("Invalid server_port in " + source + ", keeping default.");
        }
        if (j.contains("cache_size") && j["cache_size"].is_number_integer()) {
            int v = j["cache_size"].get<int>();
            if (v > 0) cfg.cache_size = v;
            else log_warn("Invalid cache_size in " + source + ", keeping default.");
        }
        if (j.contains("thread_pool_size") && j["thread_pool_size"].is_number_integer()) {
            int v = j["thread_pool_size"].get<int>();
            if (v > 0) cfg.thread_pool_size = v;
            else log_warn("Invalid thread_pool_size in " + source + ", keeping default.");
        }
        if (j.contains("database_path") && j["database_path"].is_string()) {
            cfg.database_path = j["database_path"].get<std::string>();
        }
        if (j.contains("log_level") && j["log_level"].is_string()) {
            cfg.log_level = j["log_level"].get<std::string>();
        }
    } catch (const std::exception& e) {
        log_warn(std::string("Error parsing JSON fields from ") + source + ": " + e.what());
    }
}

} // namespace

Config load_config() {
    // -------- defaults --------
    Config cfg;
    cfg.server_port = DEFAULT_SERVER_PORT;
    cfg.cache_size = DEFAULT_CACHE_CAPACITY;

    unsigned hw = std::thread::hardware_concurrency();
    cfg.thread_pool_size = (hw == 0 ? 8 : std::max(2u, hw)); // sensible default
    cfg.database_path = "kv_store.db";
    cfg.log_level = "INFO";

    // -------- candidates --------
    std::vector<std::string> candidates;
    if (const char* env = std::getenv("KV_SERVER_CONFIG")) {
        if (env[0] != '\0') candidates.emplace_back(env);
    }
    candidates.emplace_back("config/server_config.json");
    candidates.emplace_back("./server_config.json");

    // -------- load first existing --------
    for (const auto& path : candidates) {
        if (!file_exists(path)) continue;
        auto text = read_file(path);
        if (text.empty()) {
            log_warn("Config file exists but is empty/unreadable: " + path);
            continue;
        }
        try {
            auto j = json::parse(text);
            apply_json(cfg, j, path);
            log_info("Loaded config from: " + path);
            break; // stop at first successfully parsed config
        } catch (const std::exception& e) {
            log_warn(std::string("Failed to parse config at ") + path + ": " + e.what());
            // keep trying other candidates
        }
    }

    // -------- sanity clamps --------
    if (cfg.server_port <= 0 || cfg.server_port >= 65536) {
        log_warn("server_port out of range; resetting to default.");
        cfg.server_port = DEFAULT_SERVER_PORT;
    }
    if (cfg.cache_size <= 0) {
        log_warn("cache_size must be > 0; resetting to default.");
        cfg.cache_size = DEFAULT_CACHE_CAPACITY;
    }
    if (cfg.thread_pool_size <= 0) {
        log_warn("thread_pool_size must be > 0; resetting to hardware default.");
        unsigned hw2 = std::thread::hardware_concurrency();
        cfg.thread_pool_size = (hw2 == 0 ? 8 : std::max(2u, hw2));
    }
    if (cfg.database_path.empty()) {
        log_warn("database_path empty; resetting to kv_store.db");
        cfg.database_path = "kv_store.db";
    }

    // -------- summary --------
    log_info("Config effective:");
    log_info("  server_port     = " + std::to_string(cfg.server_port));
    log_info("  cache_size      = " + std::to_string(cfg.cache_size));
    log_info("  thread_pool_size= " + std::to_string(cfg.thread_pool_size));
    log_info("  database_path   = " + cfg.database_path);
    log_info("  log_level       = " + cfg.log_level);

    return cfg;
}

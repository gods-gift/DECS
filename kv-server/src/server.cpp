#include "server.h"
#include "cache.h"
#include "config.h"
#include "database.h"
#include "utils.h"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <cstddef>
#include <string>

using json = nlohmann::json;

namespace {

// global request / error counters for /metrics
std::atomic<std::size_t> g_requests{0};
std::atomic<std::size_t> g_errors{0};

std::string extract_key(const httplib::Request &req) {
    // Handlers are registered with regex like "/get/(.+)" so key is matches[1]
    if (req.matches.size() >= 2) {
        return url_decode(req.matches[1].str());
    }
    return {};
}

std::string extract_value(const httplib::Request &req) {
    // Tests and loadgen send value in ?value=..., fall back to body
    try {
        auto v = req.get_param_value("value");
        if (!v.empty()) {
            return url_decode(v);
        }
    } catch (...) {
        // no param; ignore
    }
    return req.body;
}

} // namespace

void run_server(const Config& cfg) {
    // logging level from config
    log_set_level(cfg.log_level);

    // Optional: CPU affinity
    if (!cfg.cpu_affinity.empty()) {
        std::string err;
        if (!set_process_affinity(cfg.cpu_affinity, &err)) {
            log_warn("Failed to set CPU affinity: " + err);
        } else {
            log_info("Set CPU affinity to: " + cfg.cpu_affinity);
        }
    }

    // Initialise DB
    if (!db_init(cfg)) {
        log_error("db_init failed; aborting server startup");
        return;
    }

    // In-memory cache
    LRUCache cache(cfg.cache_size);

    httplib::Server svr;
    
    // Configure thread pool size (if > 0)
    if (cfg.thread_pool_size > 0) {
        svr.new_task_queue = [&cfg] {
            return new httplib::ThreadPool(static_cast<size_t>(cfg.thread_pool_size));
        };
    }

    // --- /health -----------------------------------------------------------
    svr.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
        res.set_content("OK", "text/plain");
    });

    // --- /metrics ----------------------------------------------------------
    svr.Get("/metrics", [&cache, &cfg](const httplib::Request&, httplib::Response& res) {
        json j;
        j["requests_total"] = g_requests.load(std::memory_order_relaxed);
        j["errors_total"]   = g_errors.load(std::memory_order_relaxed);
        j["cache_hits"]     = cache.hits();
        j["cache_misses"]   = cache.misses();
        j["cache_capacity"] = cfg.cache_size;

        res.status = 200;
        res.set_content(j.dump(), "application/json");
    });

    // --- PUT /put/<key>?value=... -----------------------------------------
    svr.Put(R"(/put/(.+))", [&cache](const httplib::Request& req, httplib::Response& res) {
        g_requests.fetch_add(1, std::memory_order_relaxed);

        std::string key = extract_key(req);
        if (key.empty()) {
            g_errors.fetch_add(1, std::memory_order_relaxed);
            res.status = 400;
            res.set_content("Missing key", "text/plain");
            return;
        }

        std::string value = extract_value(req);

        if (!db_put(key, value)) {
            g_errors.fetch_add(1, std::memory_order_relaxed);
            res.status = 500;
            res.set_content("DB error", "text/plain");
            return;
        }

        cache.put(key, value);

        res.status = 200;
        // tests don’t look at PUT body, but returning value is convenient
        res.set_content(value, "text/plain");
    });

    // --- GET /get/<key> ----------------------------------------------------
    svr.Get(R"(/get/(.+))", [&cache](const httplib::Request& req, httplib::Response& res) {
        g_requests.fetch_add(1, std::memory_order_relaxed);

        std::string key = extract_key(req);
        if (key.empty()) {
            g_errors.fetch_add(1, std::memory_order_relaxed);
            res.status = 400;
            res.set_content("Missing key", "text/plain");
            return;
        }

        std::string value;

        // 1) try cache
        if (cache.get(key, value)) {
            res.status = 200;
            res.set_content(value, "text/plain");
            return;
        }

        // 2) fall back to DB
        if (!db_get(key, value)) {
            // For this project, false means "not found"
            res.status = 404;
            res.set_content("Not found", "text/plain");
            return;
        }

        // populate cache on DB hit
        cache.put(key, value);

        res.status = 200;
        res.set_content(value, "text/plain");
    });

    // --- DELETE /delete/<key> ----------------------------------------------
    svr.Delete(R"(/delete/(.+))", [&cache](const httplib::Request& req, httplib::Response& res) {
        g_requests.fetch_add(1, std::memory_order_relaxed);

        std::string key = extract_key(req);
        if (key.empty()) {
            g_errors.fetch_add(1, std::memory_order_relaxed);
            res.status = 400;
            res.set_content("Missing key", "text/plain");
            return;
        }

        bool db_ok = db_delete(key);

        // best-effort cache invalidation
        cache.erase(key);

        // tests accept either 200 or 404, but we distinguish:
        if (!db_ok) {
            res.status = 404;
            res.set_content("Not found", "text/plain");
            return;
        }

        res.status = 200;
        res.set_content("Deleted", "text/plain");
    });

    // --- Start server ------------------------------------------------------
    log_info("HTTP server starting on port " + std::to_string(cfg.server_port));

    if (!svr.listen("0.0.0.0", cfg.server_port)) {
        log_error("Server.listen failed");
    }

    db_close();
}

// src/server.cpp
#include <cassert>
#include <cctype>
#include <sstream>
#include <string>
#include <mutex>
#include <httplib.h>

#include "server.h"
#include "cache.h"
#include "database.h"
#include "config.h"
#include "utils.h"

namespace {

// Percent-decoding for path segments (e.g., "a%2Fb" -> "a/b")
std::string url_decode(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()
            && std::isxdigit(static_cast<unsigned char>(s[i+1]))
            && std::isxdigit(static_cast<unsigned char>(s[i+2]))) {
            auto hex = [](char c)->int {
                if (c >= '0' && c <= '9') return c - '0';
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                return 0;
            };
            int hi = hex(s[i+1]);
            int lo = hex(s[i+2]);
            out.push_back(static_cast<char>((hi << 4) | lo));
            i += 2;
        } else if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// Thread-safe wrapper around LRUCache
class TSCache {
public:
    explicit TSCache(size_t cap) : cache_(cap) {}
    bool get(const std::string& k, std::string& v) {
        std::lock_guard<std::mutex> lk(mu_);
        return cache_.get(k, v);
    }
    void put(const std::string& k, const std::string& v) {
        std::lock_guard<std::mutex> lk(mu_);
        cache_.put(k, v);
    }
    bool erase(const std::string& k) {
        std::lock_guard<std::mutex> lk(mu_);
        return cache_.erase(k);
    }
    size_t size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return cache_.size();
    }
private:
    mutable std::mutex mu_;
    LRUCache cache_;
};

// Helper to set common response headers/content-type
inline void respond_plain(httplib::Response& res, int status, const std::string& body) {
    res.status = status;
    res.set_content(body, "text/plain");
}

} // namespace

void run_server(const Config& cfg) {
    // 1) Init DB
    if (!db_init(cfg)) {
        log_error("Database initialization failed; shutting down.");
        return;
    }

    // 2) Construct cache
    TSCache cache(static_cast<size_t>(cfg.cache_size));
    std::ostringstream oss;
    oss << "Starting server: port=" << cfg.server_port
        << " cache_size=" << cfg.cache_size
        << " db=" << cfg.database_path
        << " threads=" << cfg.thread_pool_size;
    log_info(oss.str());

    // 3) HTTP server
    httplib::Server svr;

    // Server timeouts (tweak as needed)
    svr.set_keep_alive_max_count(100);
    svr.set_read_timeout(5, 0);   // 5s
    svr.set_write_timeout(5, 0);  // 5s

    // Healthcheck
    svr.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        respond_plain(res, 200, "OK");
    });

    // READ: GET /get/{key}
    // Capture any path (including slashes) by using .+ and decoding percent-encoding.
    svr.Get(R"(/get/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
        assert(req.matches.size() >= 2);
        const std::string key = url_decode(req.matches[1].str());

        std::string value;
        // Cache first
        if (cache.get(key, value)) {
            log_info("GET (cache hit): key=" + key);
            respond_plain(res, 200, value);
            return;
        }

        // DB on miss
        if (db_get(key, value)) {
            log_info("GET (db miss->hit): key=" + key);
            cache.put(key, value);           // populate cache
            respond_plain(res, 200, value);
        } else {
            log_warn("GET (not found): key=" + key);
            respond_plain(res, 404, "Key not found");
        }
    });

    // CREATE/UPDATE: POST /put/{key}/{value}
    // Note: value is also sent as the request body (plain text) by the provided client;
    // we prefer the URL part to keep parity with earlier API, but you can switch to JSON later.
    svr.Post(R"(/put/([^/]+)/(.*))", [&](const httplib::Request& req, httplib::Response& res) {
        assert(req.matches.size() >= 3);
        const std::string key = url_decode(req.matches[1].str());
        const std::string value = url_decode(req.matches[2].str());
        // Persist to DB first
        if (!db_put(key, value)) {
            log_error("POST failed (DB): key=" + key);
            respond_plain(res, 500, "DB error");
            return;
        }
        // Update cache
        cache.put(key, value);
        log_info("POST upsert: key=" + key);
        respond_plain(res, 200, "OK");
    });

    // DELETE: DELETE /delete/{key}
    svr.Delete(R"(/delete/(.+))", [&](const httplib::Request& req, httplib::Response& res) {
        assert(req.matches.size() >= 2);
        const std::string key = url_decode(req.matches[1].str());

        // Delete in DB is authoritative
        if (!db_delete(key)) {
            // If your DB layer returns false for "not found", treat as 404.
            log_warn("DELETE (not found): key=" + key);
            respond_plain(res, 404, "Key not found");
            return;
        }
        // Invalidate cache if present
        cache.erase(key);
        log_info("DELETE ok: key=" + key);
        respond_plain(res, 200, "OK");
    });

    // 4) Listen (blocking)
    if (!svr.listen("0.0.0.0", cfg.server_port)) {
        log_error("Server failed to bind/listen on port " + std::to_string(cfg.server_port));
    }

    // 5) Cleanup
    db_close();
}

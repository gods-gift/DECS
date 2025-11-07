// tests/test_server.cpp
#include <chrono>
#include <cstdlib>
#include <cstdio>      // std::remove
#include <iostream>
#include <string>
#include <thread>

#include "server.h"
#include "config.h"
#include "utils.h"
#include <httplib.h>

// ---------------- tiny test harness ------------------------------------
struct TestContext {
    int passed = 0;
    int failed = 0;
};

#define ASSERT_TRUE(ctx, cond) do {                                      \
    if (!(cond)) {                                                       \
        std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__        \
                  << "  Condition failed: " #cond << "\n";               \
        (ctx).failed++;                                                  \
        return;                                                          \
    }                                                                    \
} while(0)

#define ASSERT_EQ(ctx, a, b) do {                                        \
    auto _va = (a);                                                      \
    auto _vb = (b);                                                      \
    if (!((_va) == (_vb))) {                                             \
        std::cerr << "[FAIL] " << __FUNCTION__ << ":" << __LINE__        \
                  << "  Expected: " << #a << " == " << #b                \
                  << "  (" << _va << " vs " << _vb << ")\n";             \
        (ctx).failed++;                                                  \
        return;                                                          \
    }                                                                    \
} while(0)

#define TEST_CASE(ctx, name)                                             \
    void name(TestContext&);                                             \
    do {                                                                 \
        try {                                                            \
            name(ctx);                                                   \
            if (ctx.failed == 0) {                                       \
                ctx.passed++;                                            \
                std::cout << "[PASS] " #name << "\n";                    \
            } else {                                                     \
                std::cout << "[DONE] " #name << " (had failures)\n";     \
            }                                                            \
        } catch (const std::exception& e) {                              \
            std::cerr << "[EXCEPTION] " #name << ": " << e.what() << "\n";\
            ctx.failed++;                                                \
        }                                                                \
    } while(0)

// ---------------- helpers ----------------------------------------------

static std::string test_db_path() { return "test_server.db"; }

static Config make_test_config() {
    Config cfg;
    cfg.server_port = 18081;         // test port (avoid 8080 conflicts)
    cfg.cache_size = 8;
    cfg.thread_pool_size = 4;
    cfg.database_path = test_db_path();
    cfg.log_level = "ERROR";         // keep test output quiet
    return cfg;
}

static void cleanup_db() {
    std::remove(test_db_path().c_str());
}

static void start_server_detached(const Config& cfg) {
    // Launch server in a detached thread. It blocks inside run_server().
    std::thread([cfg]() {
        // set log level for server thread too
        log_set_level(cfg.log_level);
        run_server(cfg);
    }).detach();
}

static bool wait_for_health(const std::string& host, int port, int timeout_ms = 5000) {
    httplib::Client cli(host, port);
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto res = cli.Get("/health")) {
            if (res->status == 200) return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return false;
}

// ---------------- tests -------------------------------------------------

void test_health_and_basic_crud(TestContext& ctx) {
    cleanup_db();
    auto cfg = make_test_config();

    // Start server
    start_server_detached(cfg);

    // Wait until it is accepting connections
    ASSERT_TRUE(ctx, wait_for_health("localhost", cfg.server_port, 7000));

    httplib::Client cli("localhost", cfg.server_port);

    // Not-found GET
    if (auto res = cli.Get("/get/missing"); res) {
        ASSERT_EQ(ctx, res->status, 404);
    } else {
        ASSERT_TRUE(ctx, false);
    }

    // PUT key=value
    if (auto res = cli.Post("/put/user123/hello", "hello", "text/plain"); res) {
        ASSERT_EQ(ctx, res->status, 200);
    } else {
        ASSERT_TRUE(ctx, false);
    }

    // GET should now return value
    if (auto res = cli.Get("/get/user123"); res) {
        ASSERT_EQ(ctx, res->status, 200);
        ASSERT_EQ(ctx, res->body, std::string("hello"));
    } else {
        ASSERT_TRUE(ctx, false);
    }

    // Update same key
    if (auto res = cli.Post("/put/user123/world", "world", "text/plain"); res) {
        ASSERT_EQ(ctx, res->status, 200);
    } else {
        ASSERT_TRUE(ctx, false);
    }

    // GET should now return updated value
    if (auto res = cli.Get("/get/user123"); res) {
        ASSERT_EQ(ctx, res->status, 200);
        ASSERT_EQ(ctx, res->body, std::string("world"));
    } else {
        ASSERT_TRUE(ctx, false);
    }

    // DELETE key
    if (auto res = cli.Delete("/delete/user123"); res) {
        ASSERT_EQ(ctx, res->status, 200);
    } else {
        ASSERT_TRUE(ctx, false);
    }

    // GET after delete should be 404
    if (auto res = cli.Get("/get/user123"); res) {
        ASSERT_EQ(ctx, res->status, 404);
    } else {
        ASSERT_TRUE(ctx, false);
    }

    // Cleanup DB file after test completes
    cleanup_db();
}

// ---------------- main --------------------------------------------------

int main() {
    TestContext ctx;

    TEST_CASE(ctx, test_health_and_basic_crud);

    std::cout << "-----------------------------------------\n";
    std::cout << "Server tests: passed=" << ctx.passed
              << " failed=" << ctx.failed << "\n";

    // We launched the server in a detached thread and didn't add a shutdown route.
    // Exiting main() will terminate the process and thus the server thread.
    return (ctx.failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

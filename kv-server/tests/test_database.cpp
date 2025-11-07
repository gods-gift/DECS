// tests/test_database.cpp
#include <iostream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>    // std::remove
#include <stdexcept>

#include "config.h"
#include "database.h"
#include "utils.h"

// --- tiny test harness (same style as test_cache) ----------------------
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

// --- helpers ------------------------------------------------------------

static std::string tmp_db_path() {
    // Keep it stable/name-spaced for repeated runs.
    return "test_kv_store.db";
}

static Config make_test_config() {
    Config cfg;
    cfg.server_port = 18080;
    cfg.cache_size = 8;
    cfg.thread_pool_size = 2;
    cfg.database_path = tmp_db_path();
    cfg.log_level = "ERROR"; // keep tests quiet; flip to INFO/DEBUG if needed
    return cfg;
}

static void cleanup_db_file() {
    std::remove(tmp_db_path().c_str());
}

// --- tests --------------------------------------------------------------

void test_init_and_empty_get(TestContext& ctx) {
    cleanup_db_file();

    Config cfg = make_test_config();
    log_set_level(cfg.log_level);

    ASSERT_TRUE(ctx, db_init(cfg));

    std::string v;
    // key shouldn't exist yet
    ASSERT_TRUE(ctx, !db_get("missing-key", v));

    db_close();
    cleanup_db_file();
}

void test_put_and_get(TestContext& ctx) {
    cleanup_db_file();
    Config cfg = make_test_config();
    log_set_level(cfg.log_level);
    ASSERT_TRUE(ctx, db_init(cfg));

    ASSERT_TRUE(ctx, db_put("k1", "v1"));
    std::string v;
    ASSERT_TRUE(ctx, db_get("k1", v));
    ASSERT_EQ(ctx, v, std::string("v1"));

    db_close();
    cleanup_db_file();
}

void test_update_existing_key(TestContext& ctx) {
    cleanup_db_file();
    Config cfg = make_test_config();
    log_set_level(cfg.log_level);
    ASSERT_TRUE(ctx, db_init(cfg));

    ASSERT_TRUE(ctx, db_put("k1", "v1"));
    ASSERT_TRUE(ctx, db_put("k1", "v2")); // upsert/update
    std::string v;
    ASSERT_TRUE(ctx, db_get("k1", v));
    ASSERT_EQ(ctx, v, std::string("v2"));

    db_close();
    cleanup_db_file();
}

void test_delete_semantics(TestContext& ctx) {
    cleanup_db_file();
    Config cfg = make_test_config();
    log_set_level(cfg.log_level);
    ASSERT_TRUE(ctx, db_init(cfg));

    // deleting missing should return false
    ASSERT_TRUE(ctx, !db_delete("nope"));

    ASSERT_TRUE(ctx, db_put("k2", "v2"));
    ASSERT_TRUE(ctx, db_delete("k2")); // now it exists → true

    std::string v;
    ASSERT_TRUE(ctx, !db_get("k2", v)); // now gone

    db_close();
    cleanup_db_file();
}

void test_bulk_insert(TestContext& ctx) {
    cleanup_db_file();
    Config cfg = make_test_config();
    log_set_level(cfg.log_level);
    ASSERT_TRUE(ctx, db_init(cfg));

    // Insert a modest number of rows
    const int N = 200;
    for (int i = 0; i < N; ++i) {
        ASSERT_TRUE(ctx, db_put("key" + std::to_string(i), "val" + std::to_string(i)));
    }
    // spot check a few
    for (int i = 0; i < N; i += 37) {
        std::string v;
        ASSERT_TRUE(ctx, db_get("key" + std::to_string(i), v));
        ASSERT_EQ(ctx, v, "val" + std::to_string(i));
    }

    db_close();
    cleanup_db_file();
}

// --- main ---------------------------------------------------------------

int main() {
    TestContext ctx;

    TEST_CASE(ctx, test_init_and_empty_get);
    TEST_CASE(ctx, test_put_and_get);
    TEST_CASE(ctx, test_update_existing_key);
    TEST_CASE(ctx, test_delete_semantics);
    TEST_CASE(ctx, test_bulk_insert);

    std::cout << "-----------------------------------------\n";
    std::cout << "Database tests: passed=" << ctx.passed
              << " failed=" << ctx.failed << "\n";

    return (ctx.failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

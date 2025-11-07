// tests/test_cache.cpp
#include <iostream>
#include <string>
#include <vector>
#include <stdexcept>
#include <cstdlib>

#include "cache.h"

// --- tiny test harness -------------------------------------------------
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

// --- tests -------------------------------------------------------------

// Basic insert & get; update should keep size same and move to MRU
void test_basic_put_get_update(TestContext& ctx) {
    LRUCache c(3);
    std::string v;

    c.put("a", "1");
    c.put("b", "2");
    c.put("c", "3");

    ASSERT_EQ(ctx, c.size(), static_cast<size_t>(3));
    ASSERT_TRUE(ctx, c.get("a", v)); ASSERT_EQ(ctx, v, std::string("1"));
    ASSERT_TRUE(ctx, c.get("b", v)); ASSERT_EQ(ctx, v, std::string("2"));

    // update existing
    c.put("a", "1x");
    ASSERT_TRUE(ctx, c.get("a", v)); ASSERT_EQ(ctx, v, std::string("1x"));
    ASSERT_EQ(ctx, c.size(), static_cast<size_t>(3));
}

// Eviction order should be Least-Recently-Used
void test_eviction_order(TestContext& ctx) {
    LRUCache c(2);
    std::string v;

    c.put("a", "1"); // LRU: a
    c.put("b", "2"); // LRU: a, MRU: b
    // touch a -> a becomes MRU, b becomes LRU
    ASSERT_TRUE(ctx, c.get("a", v));
    ASSERT_EQ(ctx, v, std::string("1"));

    // insert c, should evict b
    c.put("c", "3");

    ASSERT_TRUE(ctx, c.get("a", v)); ASSERT_EQ(ctx, v, std::string("1"));
    bool hit_b = c.get("b", v);
    ASSERT_TRUE(ctx, !hit_b); // b should be evicted
    ASSERT_TRUE(ctx, c.get("c", v)); ASSERT_EQ(ctx, v, std::string("3"));
    ASSERT_EQ(ctx, c.size(), static_cast<size_t>(2));
}

// Erase semantics: erase returns true only if key existed
void test_erase(TestContext& ctx) {
    LRUCache c(2);
    std::string v;

    c.put("x", "9");
    c.put("y", "8");
    ASSERT_EQ(ctx, c.size(), static_cast<size_t>(2));

    ASSERT_TRUE(ctx, c.erase("x"));
    ASSERT_EQ(ctx, c.size(), static_cast<size_t>(1));

    bool got = c.get("x", v);
    ASSERT_TRUE(ctx, !got);

    // double erase should be false
    ASSERT_TRUE(ctx, !c.erase("x"));

    // remaining key still present
    ASSERT_TRUE(ctx, c.get("y", v));
    ASSERT_EQ(ctx, v, std::string("8"));
}

// Capacity 1 corner cases
void test_capacity_one(TestContext& ctx) {
    LRUCache c(1);
    std::string v;

    c.put("a", "1");
    ASSERT_TRUE(ctx, c.get("a", v)); ASSERT_EQ(ctx, v, std::string("1"));
    ASSERT_EQ(ctx, c.size(), static_cast<size_t>(1));

    c.put("b", "2"); // should evict a
    ASSERT_TRUE(ctx, !c.get("a", v));
    ASSERT_TRUE(ctx, c.get("b", v)); ASSERT_EQ(ctx, v, std::string("2"));
    ASSERT_EQ(ctx, c.size(), static_cast<size_t>(1));

    // update b
    c.put("b", "2x");
    ASSERT_TRUE(ctx, c.get("b", v)); ASSERT_EQ(ctx, v, std::string("2x"));
}

// Touching (get) must refresh MRU so it survives eviction
void test_touch_refreshes_mru(TestContext& ctx) {
    LRUCache c(2);
    std::string v;

    c.put("a", "1"); // LRU: a
    c.put("b", "2"); // LRU: a, MRU: b
    // touch a -> MRU becomes a, LRU becomes b
    ASSERT_TRUE(ctx, c.get("a", v));
    // insert c -> should evict b (the LRU)
    c.put("c", "3");

    ASSERT_TRUE(ctx, c.get("a", v)); ASSERT_EQ(ctx, v, std::string("1"));
    ASSERT_TRUE(ctx, !c.get("b", v));
    ASSERT_TRUE(ctx, c.get("c", v)); ASSERT_EQ(ctx, v, std::string("3"));
}

// --- main --------------------------------------------------------------
int main() {
    TestContext ctx;

    TEST_CASE(ctx, test_basic_put_get_update);
    TEST_CASE(ctx, test_eviction_order);
    TEST_CASE(ctx, test_erase);
    TEST_CASE(ctx, test_capacity_one);
    TEST_CASE(ctx, test_touch_refreshes_mru);

    std::cout << "-----------------------------------------\n";
    std::cout << "Cache tests: passed=" << ctx.passed
              << " failed=" << ctx.failed << "\n";

    return (ctx.failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

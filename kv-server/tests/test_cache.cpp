#include "cache.h"
#include "utils.h"

#include <cassert>
#include <iostream>

int main() {
    log_set_level("ERROR");
    LRUCache cache(2);

    cache.put("k1", "v1");
    cache.put("k2", "v2");

    std::string v;
    bool ok = cache.get("k1", v);
    assert(ok && v == "v1");

    // Evict least-recently used
    cache.put("k3", "v3");
    ok = cache.get("k2", v);
    assert(!ok);               // k2 should be evicted
    ok = cache.get("k1", v);
    assert(ok);
    ok = cache.get("k3", v);
    assert(ok);

    std::cout << "test-cache OK\n";
    return 0;
}

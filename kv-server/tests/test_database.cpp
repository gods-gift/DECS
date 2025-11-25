#include "config.h"
#include "database.h"
#include "utils.h"

#include <cassert>
#include <iostream>

int main() {
    log_set_level("INFO");

    Config cfg;
    // Adjust if your credentials differ
    cfg.pg_conninfo =
        "host=127.0.0.1 port=5432 dbname=kvdb user=kvuser password=skeys";
    cfg.pg_pool_size = 2;

    bool ok = db_init(cfg);
    assert(ok);

    ok = db_put("test-key", "hello");
    assert(ok);

    std::string value;
    ok = db_get("test-key", value);
    assert(ok && value == "hello");

    ok = db_delete("test-key");
    assert(ok);

    ok = db_get("test-key", value);
    assert(!ok);

    db_close();
    std::cout << "test-database OK\n";
    return 0;
}

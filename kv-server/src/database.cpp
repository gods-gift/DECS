// src/database.cpp
#include "database.h"
#include "config.h"
#include "utils.h"

#include <mutex>
#include <string>
#include <sstream>

#ifdef DB_BACKEND_SQLITE
  #include <sqlite3.h>
#elif defined(DB_BACKEND_POSTGRES) || defined(DB_BACKEND_MYSQL)
  // Placeholders for other backends – see the #else at bottom.
#else
  #error "Define one DB backend (DB_BACKEND_SQLITE | DB_BACKEND_POSTGRES | DB_BACKEND_MYSQL)"
#endif

namespace {

#ifdef DB_BACKEND_SQLITE

sqlite3* g_db = nullptr;
std::mutex g_db_mu;

// Prepared statements
sqlite3_stmt* g_stmt_upsert = nullptr;  // INSERT ... ON CONFLICT DO UPDATE
sqlite3_stmt* g_stmt_get    = nullptr;  // SELECT value FROM kv_store WHERE key=?
sqlite3_stmt* g_stmt_delete = nullptr;  // DELETE FROM kv_store WHERE key=?

// Finalizer helper
void finalize_stmt(sqlite3_stmt*& stmt) {
    if (stmt) {
        sqlite3_finalize(stmt);
        stmt = nullptr;
    }
}

// Exec helper with logging
bool exec_sql(sqlite3* db, const char* sql) {
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db, sql, nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::ostringstream oss;
        oss << "SQLite exec failed (" << rc << "): " << (errmsg ? errmsg : "(null)")
            << " SQL: " << sql;
        log_error(oss.str());
        if (errmsg) sqlite3_free(errmsg);
        return false;
    }
    return true;
}

#endif // DB_BACKEND_SQLITE

} // namespace

// ------------------------- Public API -----------------------------------

bool db_init(const Config& cfg) {
#ifdef DB_BACKEND_SQLITE
    std::lock_guard<std::mutex> lk(g_db_mu);

    if (g_db) {
        log_warn("db_init called but DB already open; reusing existing connection.");
        return true;
    }

    // Open
    int rc = sqlite3_open(cfg.database_path.c_str(), &g_db);
    if (rc != SQLITE_OK) {
        std::ostringstream oss;
        oss << "sqlite3_open failed: " << sqlite3_errmsg(g_db) << " path=" << cfg.database_path;
        log_error(oss.str());
        if (g_db) { sqlite3_close(g_db); g_db = nullptr; }
        return false;
    }

    // Busy timeout so concurrent writers don't fail immediately
    sqlite3_busy_timeout(g_db, 5000); // 5s

    // Pragmas: WAL for concurrency, reasonable sync level
    exec_sql(g_db, "PRAGMA journal_mode=WAL;");
    exec_sql(g_db, "PRAGMA synchronous=FULL;");
    exec_sql(g_db, "PRAGMA temp_store=MEMORY;");
    exec_sql(g_db, "PRAGMA mmap_size=268435456;"); // 256MB (best-effort)

    // Schema
    if (!exec_sql(g_db,
        "CREATE TABLE IF NOT EXISTS kv_store ("
        "  key   TEXT PRIMARY KEY,"
        "  value TEXT"
        ");"))
    {
        sqlite3_close(g_db); g_db = nullptr;
        return false;
    }

    // Prepared statements
    const char* SQL_UPSERT =
        "INSERT INTO kv_store(key, value) VALUES(?, ?) "
        "ON CONFLICT(key) DO UPDATE SET value=excluded.value;";
    const char* SQL_GET =
        "SELECT value FROM kv_store WHERE key=?;";
    const char* SQL_DELETE =
        "DELETE FROM kv_store WHERE key=?;";

    if (sqlite3_prepare_v2(g_db, SQL_UPSERT, -1, &g_stmt_upsert, nullptr) != SQLITE_OK) {
        log_error(std::string("prepare UPSERT failed: ") + sqlite3_errmsg(g_db));
        db_close();
        return false;
    }
    if (sqlite3_prepare_v2(g_db, SQL_GET, -1, &g_stmt_get, nullptr) != SQLITE_OK) {
        log_error(std::string("prepare GET failed: ") + sqlite3_errmsg(g_db));
        db_close();
        return false;
    }
    if (sqlite3_prepare_v2(g_db, SQL_DELETE, -1, &g_stmt_delete, nullptr) != SQLITE_OK) {
        log_error(std::string("prepare DELETE failed: ") + sqlite3_errmsg(g_db));
        db_close();
        return false;
    }

    log_info("SQLite DB initialized: " + cfg.database_path);
    return true;

#elif defined(DB_BACKEND_POSTGRES)
    log_error("PostgreSQL backend not implemented in database.cpp. Enable DB_BACKEND_SQLITE or add a PG implementation.");
    return false;

#elif defined(DB_BACKEND_MYSQL)
    log_error("MySQL backend not implemented in database.cpp. Enable DB_BACKEND_SQLITE or add a MySQL implementation.");
    return false;

#else
    return false;
#endif
}

bool db_get(const std::string& key, std::string& value_out) {
#ifdef DB_BACKEND_SQLITE
    std::lock_guard<std::mutex> lk(g_db_mu);
    if (!g_db || !g_stmt_get) return false;

    // Bind key
    sqlite3_reset(g_stmt_get);
    sqlite3_clear_bindings(g_stmt_get);
    if (sqlite3_bind_text(g_stmt_get, 1, key.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        log_error(std::string("GET bind failed: ") + sqlite3_errmsg(g_db));
        return false;
    }

    int rc = sqlite3_step(g_stmt_get);
    if (rc == SQLITE_ROW) {
        const unsigned char* txt = sqlite3_column_text(g_stmt_get, 0);
        if (txt) value_out.assign(reinterpret_cast<const char*>(txt));
        else     value_out.clear();
        sqlite3_reset(g_stmt_get);
        return true;
    } else if (rc == SQLITE_DONE) {
        // not found
        sqlite3_reset(g_stmt_get);
        return false;
    } else {
        log_error(std::string("GET step failed: ") + sqlite3_errmsg(g_db));
        sqlite3_reset(g_stmt_get);
        return false;
    }

#else
    (void)key; (void)value_out;
    return false;
#endif
}

bool db_put(const std::string& key, const std::string& value) {
#ifdef DB_BACKEND_SQLITE
    std::lock_guard<std::mutex> lk(g_db_mu);
    if (!g_db || !g_stmt_upsert) return false;

    sqlite3_reset(g_stmt_upsert);
    sqlite3_clear_bindings(g_stmt_upsert);

    if (sqlite3_bind_text(g_stmt_upsert, 1, key.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        log_error(std::string("PUT bind key failed: ") + sqlite3_errmsg(g_db));
        return false;
    }
    if (sqlite3_bind_text(g_stmt_upsert, 2, value.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        log_error(std::string("PUT bind value failed: ") + sqlite3_errmsg(g_db));
        return false;
    }

    int rc = sqlite3_step(g_stmt_upsert);
    if (rc == SQLITE_DONE) {
        sqlite3_reset(g_stmt_upsert);
        return true;
    } else {
        log_error(std::string("PUT step failed: ") + sqlite3_errmsg(g_db));
        sqlite3_reset(g_stmt_upsert);
        return false;
    }

#else
    (void)key; (void)value;
    return false;
#endif
}

bool db_delete(const std::string& key) {
#ifdef DB_BACKEND_SQLITE
    std::lock_guard<std::mutex> lk(g_db_mu);
    if (!g_db || !g_stmt_delete) return false;

    sqlite3_reset(g_stmt_delete);
    sqlite3_clear_bindings(g_stmt_delete);

    if (sqlite3_bind_text(g_stmt_delete, 1, key.c_str(), -1, SQLITE_TRANSIENT) != SQLITE_OK) {
        log_error(std::string("DELETE bind failed: ") + sqlite3_errmsg(g_db));
        return false;
    }

    int rc = sqlite3_step(g_stmt_delete);
    if (rc == SQLITE_DONE) {
        int changes = sqlite3_changes(g_db);
        sqlite3_reset(g_stmt_delete);
        // Return true only if a row was actually deleted; your server turns this into 404/200 as needed
        return changes > 0;
    } else {
        log_error(std::string("DELETE step failed: ") + sqlite3_errmsg(g_db));
        sqlite3_reset(g_stmt_delete);
        return false;
    }

#else
    (void)key;
    return false;
#endif
}

void db_close() {
#ifdef DB_BACKEND_SQLITE
    std::lock_guard<std::mutex> lk(g_db_mu);
    if (!g_db) return;

    finalize_stmt(g_stmt_upsert);
    finalize_stmt(g_stmt_get);
    finalize_stmt(g_stmt_delete);

    sqlite3_close(g_db);
    g_db = nullptr;
    log_info("SQLite DB closed.");
#endif
}

#include "database.h"
#include "utils.h"

#include <libpq-fe.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace {

struct ConnSlot {
    PGconn* conn = nullptr;
    std::mutex mu;
};

std::vector<std::unique_ptr<ConnSlot>> g_pool;
std::atomic<uint64_t> g_rr{0};
bool g_inited = false;

constexpr const char* STMT_UPSERT = "kv_upsert";
constexpr const char* STMT_SELECT = "kv_select";
constexpr const char* STMT_DELETE = "kv_delete";

inline bool exec_ok(PGresult* r) {
    if (!r) return false;
    auto s = PQresultStatus(r);
    return s == PGRES_COMMAND_OK || s == PGRES_TUPLES_OK;
}

bool ensure_table(PGconn* c) {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS kv_store ("
        "  key   TEXT PRIMARY KEY,"
        "  value TEXT NOT NULL"
        ");";

    PGresult* r = PQexec(c, sql);
    bool ok = (r && PQresultStatus(r) == PGRES_COMMAND_OK);
    if (!ok) {
        log_error(std::string("CREATE TABLE failed: ") + PQerrorMessage(c));
    }
    if (r) PQclear(r);
    return ok;
}

bool prepare_on(PGconn* c) {
    {
        const char* sql =
            "INSERT INTO kv_store(key,value) VALUES($1,$2) "
            "ON CONFLICT (key) DO UPDATE SET value=EXCLUDED.value;";
        PGresult* r = PQprepare(c, STMT_UPSERT, sql, 2, nullptr);
        if (!exec_ok(r)) { if (r) PQclear(r); return false; }
        PQclear(r);
    }
    {
        const char* sql = "SELECT value FROM kv_store WHERE key=$1;";
        PGresult* r = PQprepare(c, STMT_SELECT, sql, 1, nullptr);
        if (!exec_ok(r)) { if (r) PQclear(r); return false; }
        PQclear(r);
    }
    {
        const char* sql = "DELETE FROM kv_store WHERE key=$1;";
        PGresult* r = PQprepare(c, STMT_DELETE, sql, 1, nullptr);
        if (!exec_ok(r)) { if (r) PQclear(r); return false; }
        PQclear(r);
    }
    return true;
}

ConnSlot& pick_slot() {
    const uint64_t i = g_rr.fetch_add(1, std::memory_order_relaxed);
    return *g_pool[static_cast<std::size_t>(i % g_pool.size())];
}

} // namespace

bool db_init(const Config& cfg) {
    if (g_inited) return true;

    const int N = std::max(1, cfg.pg_pool_size);
    g_pool.clear();
    g_pool.reserve(static_cast<std::size_t>(N));
    for (int i = 0; i < N; ++i) {
        g_pool.emplace_back(std::make_unique<ConnSlot>());
    }

    for (int i = 0; i < N; ++i) {
        PGconn* c = PQconnectdb(cfg.pg_conninfo.c_str());
        if (PQstatus(c) != CONNECTION_OK) {
            log_error(std::string("PQconnectdb failed [") + std::to_string(i) + "]: " + PQerrorMessage(c));
            PQfinish(c);
            for (int j = 0; j < i; ++j) {
                if (g_pool[j] && g_pool[j]->conn) {
                    PQfinish(g_pool[j]->conn);
                    g_pool[j]->conn = nullptr;
                }
            }
            g_pool.clear();
            return false;
        }

        if (!prepare_on(c)) {
            log_error("prepare failed: " + std::string(PQerrorMessage(c)));
            PQfinish(c);
            for (int j = 0; j < i; ++j) {
                if (g_pool[j] && g_pool[j]->conn) {
                    PQfinish(g_pool[j]->conn);
                    g_pool[j]->conn = nullptr;
                }
            }
            g_pool.clear();
            return false;
        }

        g_pool[i]->conn = c;
    }

    if (!ensure_table(g_pool[0]->conn)) {
        db_close();
        return false;
    }

    g_inited = true;
    log_info("PostgreSQL pool initialized with " + std::to_string(N) + " connections.");
    return true;
}

bool db_put(const std::string& key, const std::string& value) {
    if (!g_inited || g_pool.empty()) return false;

    ConnSlot& s = pick_slot();
    std::lock_guard<std::mutex> lk(s.mu);

    const char* params[2]  = { key.c_str(), value.c_str() };
    const int   lengths[2] = { static_cast<int>(key.size()), static_cast<int>(value.size()) };
    const int   formats[2] = { 0, 0 };

    PGresult* r = PQexecPrepared(s.conn, STMT_UPSERT, 2, params, lengths, formats, 0);
    bool ok = (r && PQresultStatus(r) == PGRES_COMMAND_OK);
    if (!ok) {
        log_warn(std::string("UPSERT failed: ") + PQerrorMessage(s.conn));
    }
    if (r) PQclear(r);
    return ok;
}

bool db_get(const std::string& key, std::string& value_out) {
    if (!g_inited || g_pool.empty()) return false;

    ConnSlot& s = pick_slot();
    std::lock_guard<std::mutex> lk(s.mu);

    const char* params[1]  = { key.c_str() };
    const int   lengths[1] = { static_cast<int>(key.size()) };
    const int   formats[1] = { 0 };

    PGresult* r = PQexecPrepared(s.conn, STMT_SELECT, 1, params, lengths, formats, 0);
    if (!r || PQresultStatus(r) != PGRES_TUPLES_OK) {
        if (r) PQclear(r);
        log_warn(std::string("SELECT failed: ") + PQerrorMessage(s.conn));
        return false;
    }

    bool found = (PQntuples(r) == 1);
    if (found) {
        value_out.assign(PQgetvalue(r, 0, 0));
    }
    PQclear(r);
    return found;
}

bool db_delete(const std::string& key) {
    if (!g_inited || g_pool.empty()) return false;

    ConnSlot& s = pick_slot();
    std::lock_guard<std::mutex> lk(s.mu);

    const char* params[1]  = { key.c_str() };
    const int   lengths[1] = { static_cast<int>(key.size()) };
    const int   formats[1] = { 0 };

    PGresult* r = PQexecPrepared(s.conn, STMT_DELETE, 1, params, lengths, formats, 0);
    bool ok = (r && PQresultStatus(r) == PGRES_COMMAND_OK);
    bool existed = false;
    if (ok) {
        char* c = PQcmdTuples(r);
        if (c && *c) existed = (std::atoi(c) > 0);
    } else {
        log_warn(std::string("DELETE failed: ") + PQerrorMessage(s.conn));
    }
    if (r) PQclear(r);
    return existed;
}

void db_close() {
    for (auto& p : g_pool) {
        if (p && p->conn) {
            PQfinish(p->conn);
            p->conn = nullptr;
        }
    }
    g_pool.clear();
    g_inited = false;
    log_info("PostgreSQL pool closed.");
}

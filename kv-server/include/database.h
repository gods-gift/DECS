// include/database.h
#pragma once

#include <string>

// -----------------------------------------------------------------------------
// Database backend selection
//
// CMake should define exactly ONE of:
//   DB_BACKEND_SQLITE
//   DB_BACKEND_POSTGRES
//   DB_BACKEND_MYSQL
//
// Example (CMakeLists):
//   add_compile_definitions(DB_BACKEND_SQLITE)
//
// -----------------------------------------------------------------------------

struct Config;

/**
 * Initialize the database using fields from Config.
 *
 * For SQLite:
 *   - Opens/creates the DB file.
 *   - Configures WAL, synchronous mode, mmap_size.
 *   - Creates table kv_store(key TEXT PRIMARY KEY, value TEXT).
 *   - Prepares statements for get/put/delete.
 *
 * Return: true on success, false on fatal error.
 */
bool db_init(const Config& cfg);

/**
 * Lookup a key in the persistent store.
 *
 * @param key        Key to lookup.
 * @param value_out  Filled with the stored value on success.
 *
 * Returns:
 *   - true  if found
 *   - false if not found OR on DB error
 *
 * The server layer differentiates "not found" and errors by route logic.
 */
bool db_get(const std::string& key, std::string& value_out);

/**
 * Insert or update a key-value pair.
 * For SQLite, uses an UPSERT:
 *   INSERT ... ON CONFLICT(key) DO UPDATE
 *
 * Returns true on success, false on error.
 */
bool db_put(const std::string& key, const std::string& value);

/**
 * Delete a key from the database.
 *
 * Returns:
 *   - true  if a row was actually deleted
 *   - false if no row existed OR an error occurred
 *
 * The HTTP server maps this to 200 OK / 404 Not Found.
 */
bool db_delete(const std::string& key);

/**
 * Gracefully close the database connection, finalize statements,
 * flush WAL if enabled, etc.
 */
void db_close();

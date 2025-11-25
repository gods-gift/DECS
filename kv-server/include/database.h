#pragma once
#include <string>
#include "config.h"

/**
 * PostgreSQL-backed KV store.
 * Functions are thread-safe via internal pooling & mutexes.
 */
bool db_init(const Config& cfg);
bool db_put(const std::string& key, const std::string& value);
bool db_get(const std::string& key, std::string& value_out);
bool db_delete(const std::string& key);
void db_close();

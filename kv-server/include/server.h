// include/server.h
#pragma once

#include "config.h"

/**
 * Start the HTTP KV server.
 *
 * - Initializes the database using fields from Config
 * - Constructs the in-memory cache
 * - Registers HTTP routes:
 *     GET    /health
 *     GET    /get/{key}
 *     POST   /put/{key}/{value}
 *     DELETE /delete/{key}
 * - Blocks inside the HTTP server's listen loop until shutdown/failure
 *
 * Returns only after the server stops. Any fatal init error is logged.
 */
void run_server(const Config& cfg);

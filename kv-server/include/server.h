#pragma once
#include "config.h"

/** Blocking call: initialize DB, cache, HTTP server and run forever. */
void run_server(const Config& cfg);

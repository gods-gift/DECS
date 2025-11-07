set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
cmake_minimum_required(VERSION 3.16)
project(KVServer CXX)

# ----------------------------- Options ---------------------------------
option(BUILD_TESTS            "Build unit tests"               ON)
option(ENABLE_SSL             "Enable SSL for httplib"         OFF)
option(ENABLE_SANITIZERS      "Enable address/ub sanitizers"   OFF)

# Choose backend (default: SQLite)
add_compile_definitions(DB_BACKEND_SQLITE)

set(CACHE_CAPACITY_DEFAULT    100  CACHE STRING "Default LRU cache capacity")
set(SERVER_PORT_DEFAULT       8080 CACHE STRING "Default HTTP port")

# ----------------------------- Language/Warnings -----------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (MSVC)
  add_compile_options(/W4 /permissive- /EHsc)
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion)
  if (ENABLE_SANITIZERS)
    add_compile_options(-fsanitize=address,undefined -g)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# ----------------------------- Dependencies ----------------------------
include(FetchContent)
find_package(Threads REQUIRED)

# cpp-httplib (header-only)
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.15.3
)
FetchContent_MakeAvailable(httplib)
# expose the directory that contains httplib.h
set(HTTPLIB_INCLUDE_DIR ${httplib_SOURCE_DIR})

# nlohmann/json (header-only)
FetchContent_Declare(nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# SSL (optional, for HTTPS in httplib)
if (ENABLE_SSL)
  find_package(OpenSSL REQUIRED)
  add_compile_definitions(CPPHTTPLIB_OPENSSL_SUPPORT)
endif()

# SQLite
find_package(SQLite3 REQUIRED)

# ----------------------------- Sources/Headers -------------------------
set(PROJ_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/include)

set(SERVER_CORE_SRC
  src/server.cpp
  src/cache.cpp
  src/database.cpp
  src/config.cpp
  src/utils.cpp
)

set(SERVER_CORE_HDR
  ${PROJ_INCLUDE_DIR}/server.h
  ${PROJ_INCLUDE_DIR}/cache.h
  ${PROJ_INCLUDE_DIR}/database.h
  ${PROJ_INCLUDE_DIR}/config.h
  ${PROJ_INCLUDE_DIR}/utils.h
)

# ----------------------------- kv-server -------------------------------
add_executable(kv-server
  src/main.cpp            # <-- has int main()
  ${SERVER_CORE_SRC}
  ${SERVER_CORE_HDR}
)
target_include_directories(kv-server PRIVATE
  ${PROJ_INCLUDE_DIR}
  ${HTTPLIB_INCLUDE_DIR}
)
target_link_libraries(kv-server PRIVATE
  Threads::Threads
  nlohmann_json::nlohmann_json
  SQLite::SQLite3
)
if (ENABLE_SSL)
  target_link_libraries(kv-server PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
if (WIN32)
  target_link_libraries(kv-server PRIVATE ws2_32)
endif()
target_compile_definitions(kv-server PRIVATE
  DEFAULT_CACHE_CAPACITY=${CACHE_CAPACITY_DEFAULT}
  DEFAULT_SERVER_PORT=${SERVER_PORT_DEFAULT}
)

# ----------------------------- kv-client -------------------------------
add_executable(kv-client
  client/client.cpp       # <-- has int main()
)
target_include_directories(kv-client PRIVATE
  ${PROJ_INCLUDE_DIR}
  ${HTTPLIB_INCLUDE_DIR}
)
target_link_libraries(kv-client PRIVATE Threads::Threads)
if (ENABLE_SSL)
  target_link_libraries(kv-client PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
if (WIN32)
  target_link_libraries(kv-client PRIVATE ws2_32)
endif()

# ----------------------------- kv-loadgen ------------------------------
add_executable(kv-loadgen
  loadgen/loadgen_main.cpp   # <-- has int main()
  loadgen/load_generator.cpp
  src/utils.cpp
  src/config.cpp
  ${PROJ_INCLUDE_DIR}/load_generator.h
  ${PROJ_INCLUDE_DIR}/utils.h
  ${PROJ_INCLUDE_DIR}/config.h
)
target_include_directories(kv-loadgen PRIVATE
  ${PROJ_INCLUDE_DIR}
  ${HTTPLIB_INCLUDE_DIR}
)
target_link_libraries(kv-loadgen PRIVATE
  Threads::Threads
  nlohmann_json::nlohmann_json
)
if (WIN32)
  target_link_libraries(kv-loadgen PRIVATE ws2_32)
endif()

# ----------------------------- Tests -----------------------------------
if (BUILD_TESTS)
  enable_testing()

  # test-cache (no httplib)
  add_executable(test-cache
    tests/test_cache.cpp
    src/cache.cpp
    src/utils.cpp
    ${PROJ_INCLUDE_DIR}/cache.h
    ${PROJ_INCLUDE_DIR}/utils.h
  )
  target_include_directories(test-cache PRIVATE ${PROJ_INCLUDE_DIR})
  target_link_libraries(test-cache PRIVATE Threads::Threads)
  add_test(NAME CacheTests COMMAND test-cache)

  # test-database (no httplib)
  add_executable(test-database
    tests/test_database.cpp
    src/database.cpp
    src/config.cpp
    src/utils.cpp
    ${PROJ_INCLUDE_DIR}/database.h
    ${PROJ_INCLUDE_DIR}/config.h
    ${PROJ_INCLUDE_DIR}/utils.h
  )
  target_include_directories(test-database PRIVATE ${PROJ_INCLUDE_DIR})
  target_link_libraries(test-database PRIVATE
    Threads::Threads
    SQLite::SQLite3
    nlohmann_json::nlohmann_json
  )
  add_test(NAME DatabaseTests COMMAND test-database)

  # test-server (needs httplib)
  add_executable(test-server
    tests/test_server.cpp
    ${SERVER_CORE_SRC}
    ${SERVER_CORE_HDR}
  )
  target_include_directories(test-server PRIVATE
    ${PROJ_INCLUDE_DIR}
    ${HTTPLIB_INCLUDE_DIR}
  )
  target_link_libraries(test-server PRIVATE
    Threads::Threads
    nlohmann_json::nlohmann_json
    SQLite::SQLite3
  )
  if (ENABLE_SSL)
    target_link_libraries(test-server PRIVATE OpenSSL::SSL OpenSSL::Crypto)
  endif()
  if (WIN32)
    target_link_libraries(test-server PRIVATE ws2_32)
  endif()
  add_test(NAME ServerTests COMMAND test-server)
endif()

# ----------------------------- Install (optional) ----------------------
include(GNUInstallDirs)
install(TARGETS kv-server kv-client kv-loadgen
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES config/server_config.json
        DESTINATION ${CMAKE_INSTALL_DATADIR}/kv-server)

# ----------------------------- Summary ---------------------------------
message(STATUS "Build options:")
message(STATUS "  ENABLE_SSL    = ${ENABLE_SSL}")
message(STATUS "  BUILD_TESTS   = ${BUILD_TESTS}")
message(STATUS "Defaults:")
message(STATUS "  DEFAULT_CACHE_CAPACITY = ${CACHE_CAPACITY_DEFAULT}")
message(STATUS "  DEFAULT_SERVER_PORT    = ${SERVER_PORT_DEFAULT}")

cmake_minimum_required(VERSION 3.16)
project(kv_server CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTS "Build tests" ON)
option(ENABLE_SSL "Enable SSL in httplib" OFF)

find_package(Threads REQUIRED)

include(FetchContent)

# nlohmann::json
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

# cpp-httplib
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG v0.15.3
)
FetchContent_MakeAvailable(httplib)

# PostgreSQL (libpq)
find_package(PostgreSQL REQUIRED)

message(STATUS "PostgreSQL include dirs: ${PostgreSQL_INCLUDE_DIRS}")
message(STATUS "PostgreSQL libraries   : ${PostgreSQL_LIBRARIES}")

set(SERVER_SRC
    src/main.cpp
    src/server.cpp
    src/database.cpp
    src/cache.cpp
    src/config.cpp
    src/utils.cpp
)

set(CLIENT_SRC
    client/client.cpp
    src/config.cpp
    src/utils.cpp
)

set(LOADGEN_SRC
    loadgen/loadgen_main.cpp
    loadgen/load_generator.cpp
    src/config.cpp
    src/utils.cpp
)

add_executable(kv-server ${SERVER_SRC})
add_executable(kv-client ${CLIENT_SRC})
add_executable(kv-loadgen ${LOADGEN_SRC})

target_include_directories(kv-server PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${httplib_SOURCE_DIR}
    ${PostgreSQL_INCLUDE_DIRS}
)

target_include_directories(kv-client PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${httplib_SOURCE_DIR}
)

target_include_directories(kv-loadgen PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${httplib_SOURCE_DIR}
)

target_link_libraries(kv-server
    PRIVATE
        nlohmann_json::nlohmann_json
        ${PostgreSQL_LIBRARIES}
        Threads::Threads
)

target_link_libraries(kv-client
    PRIVATE
        nlohmann_json::nlohmann_json
        Threads::Threads
)

target_link_libraries(kv-loadgen
    PRIVATE
        nlohmann_json::nlohmann_json
        Threads::Threads
)

if (BUILD_TESTS)
    add_executable(test-cache
        tests/test_cache.cpp
        src/cache.cpp
        src/utils.cpp
        src/config.cpp
    )

    add_executable(test-database
        tests/test_database.cpp
        src/database.cpp
        src/utils.cpp
        src/config.cpp
    )

    add_executable(test-server
        tests/test_server.cpp
        src/server.cpp
        src/cache.cpp
        src/database.cpp
        src/utils.cpp
        src/config.cpp
    )

    target_include_directories(test-cache PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
    )

    target_include_directories(test-database PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${PostgreSQL_INCLUDE_DIRS}
    )

    target_include_directories(test-server PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${httplib_SOURCE_DIR}
        ${PostgreSQL_INCLUDE_DIRS}
    )

    target_link_libraries(test-cache PRIVATE Threads::Threads)

    target_link_libraries(test-database
        PRIVATE
            nlohmann_json::nlohmann_json
            ${PostgreSQL_LIBRARIES}
            Threads::Threads
    )

    target_link_libraries(test-server
        PRIVATE
            nlohmann_json::nlohmann_json
            ${PostgreSQL_LIBRARIES}
            Threads::Threads
    )
endif()

message(STATUS "Build options:")
message(STATUS "  ENABLE_SSL    = ${ENABLE_SSL}")
message(STATUS "  BUILD_TESTS   = ${BUILD_TESTS}")
message(STATUS "Defaults:")
message(STATUS "  DEFAULT_CACHE_CAPACITY = 100")
message(STATUS "  DEFAULT_SERVER_PORT    = 8080")

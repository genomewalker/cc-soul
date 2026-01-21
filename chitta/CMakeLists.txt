cmake_minimum_required(VERSION 3.14)
project(chitta VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)

# CRoaring - compressed bitmaps for tag indices (100M+ scale)
FetchContent_Declare(
    roaring
    GIT_REPOSITORY https://github.com/RoaringBitmap/CRoaring.git
    GIT_TAG v4.3.0
    GIT_SHALLOW TRUE
)
set(ROARING_BUILD_STATIC ON CACHE BOOL "" FORCE)
set(ENABLE_ROARING_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(roaring)

# DuckDB - embedded database with vector search and graph queries
# Uses system/conda installation, or custom paths via DUCKDB_INCLUDE_DIR/DUCKDB_LIB_DIR
find_library(DUCKDB_LIB duckdb HINTS
    "${DUCKDB_LIB_DIR}"
    "$ENV{CONDA_PREFIX}/lib"
    "/usr/local/lib"
)
find_path(DUCKDB_INCLUDE duckdb.hpp HINTS
    "${DUCKDB_INCLUDE_DIR}"
    "$ENV{CONDA_PREFIX}/include"
    "/usr/local/include"
)
if(DUCKDB_LIB AND DUCKDB_INCLUDE)
    # Use INTERFACE library instead of IMPORTED for better cross-platform compatibility
    add_library(duckdb INTERFACE)
    target_include_directories(duckdb INTERFACE "${DUCKDB_INCLUDE}")
    target_link_libraries(duckdb INTERFACE "${DUCKDB_LIB}")
    message(STATUS "DuckDB found: ${DUCKDB_LIB}")
else()
    message(FATAL_ERROR "DuckDB not found. Install with: mamba install libduckdb-devel")
endif()

# PostgreSQL - for multi-writer HPC deployments
find_library(LIBPQ_LIB pq HINTS "$ENV{CONDA_PREFIX}/lib")
find_path(LIBPQ_INCLUDE libpq-fe.h HINTS "$ENV{CONDA_PREFIX}/include")
if(LIBPQ_LIB AND LIBPQ_INCLUDE)
    add_library(libpq SHARED IMPORTED)
    set_target_properties(libpq PROPERTIES
        IMPORTED_LOCATION "${LIBPQ_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${LIBPQ_INCLUDE}"
    )
    set(CHITTA_WITH_POSTGRES ON)
    message(STATUS "PostgreSQL (libpq) found: ${LIBPQ_LIB}")
else()
    set(CHITTA_WITH_POSTGRES OFF)
    message(STATUS "PostgreSQL (libpq) not found - PostgresStore disabled")
endif()

# Tree-sitter - incremental parsing for code intelligence
FetchContent_Declare(
    tree-sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG v0.24.6
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tree-sitter)
if(NOT tree-sitter_POPULATED)
    FetchContent_Populate(tree-sitter)
    # Build tree-sitter as static library
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(${tree-sitter_SOURCE_DIR}/lib ${tree-sitter_BINARY_DIR})
endif()

# Tree-sitter C++ grammar
FetchContent_Declare(
    tree-sitter-cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG v0.23.4
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tree-sitter-cpp)
if(NOT tree-sitter-cpp_POPULATED)
    FetchContent_Populate(tree-sitter-cpp)
endif()

# Tree-sitter Python grammar
FetchContent_Declare(
    tree-sitter-python
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python.git
    GIT_TAG v0.23.6
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tree-sitter-python)
if(NOT tree-sitter-python_POPULATED)
    FetchContent_Populate(tree-sitter-python)
endif()

# Tree-sitter JavaScript grammar (includes TypeScript)
FetchContent_Declare(
    tree-sitter-javascript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-javascript.git
    GIT_TAG v0.23.1
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tree-sitter-javascript)
if(NOT tree-sitter-javascript_POPULATED)
    FetchContent_Populate(tree-sitter-javascript)
endif()

# Tree-sitter TypeScript grammar
FetchContent_Declare(
    tree-sitter-typescript
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-typescript.git
    GIT_TAG v0.23.2
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tree-sitter-typescript)
if(NOT tree-sitter-typescript_POPULATED)
    FetchContent_Populate(tree-sitter-typescript)
endif()

# Tree-sitter Go grammar
FetchContent_Declare(
    tree-sitter-go
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-go.git
    GIT_TAG v0.23.4
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tree-sitter-go)
if(NOT tree-sitter-go_POPULATED)
    FetchContent_Populate(tree-sitter-go)
endif()

# Tree-sitter Rust grammar
FetchContent_Declare(
    tree-sitter-rust
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-rust.git
    GIT_TAG v0.23.2
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tree-sitter-rust)
if(NOT tree-sitter-rust_POPULATED)
    FetchContent_Populate(tree-sitter-rust)
endif()

# Tree-sitter Java grammar
FetchContent_Declare(
    tree-sitter-java
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-java.git
    GIT_TAG v0.23.5
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tree-sitter-java)
if(NOT tree-sitter-java_POPULATED)
    FetchContent_Populate(tree-sitter-java)
endif()

# Tree-sitter Ruby grammar
FetchContent_Declare(
    tree-sitter-ruby
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-ruby.git
    GIT_TAG v0.23.1
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tree-sitter-ruby)
if(NOT tree-sitter-ruby_POPULATED)
    FetchContent_Populate(tree-sitter-ruby)
endif()

# Tree-sitter C# grammar
FetchContent_Declare(
    tree-sitter-c-sharp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-c-sharp.git
    GIT_TAG v0.23.1
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tree-sitter-c-sharp)
if(NOT tree-sitter-c-sharp_POPULATED)
    FetchContent_Populate(tree-sitter-c-sharp)
endif()

# Build tree-sitter parsers as static library
add_library(tree-sitter-parsers STATIC
    ${tree-sitter-cpp_SOURCE_DIR}/src/parser.c
    ${tree-sitter-cpp_SOURCE_DIR}/src/scanner.c
    ${tree-sitter-python_SOURCE_DIR}/src/parser.c
    ${tree-sitter-python_SOURCE_DIR}/src/scanner.c
    ${tree-sitter-javascript_SOURCE_DIR}/src/parser.c
    ${tree-sitter-javascript_SOURCE_DIR}/src/scanner.c
    ${tree-sitter-typescript_SOURCE_DIR}/typescript/src/parser.c
    ${tree-sitter-typescript_SOURCE_DIR}/typescript/src/scanner.c
    ${tree-sitter-go_SOURCE_DIR}/src/parser.c
    ${tree-sitter-rust_SOURCE_DIR}/src/parser.c
    ${tree-sitter-rust_SOURCE_DIR}/src/scanner.c
    ${tree-sitter-java_SOURCE_DIR}/src/parser.c
    ${tree-sitter-ruby_SOURCE_DIR}/src/parser.c
    ${tree-sitter-ruby_SOURCE_DIR}/src/scanner.c
    ${tree-sitter-c-sharp_SOURCE_DIR}/src/parser.c
    ${tree-sitter-c-sharp_SOURCE_DIR}/src/scanner.c
)
target_include_directories(tree-sitter-parsers PUBLIC
    ${tree-sitter_SOURCE_DIR}/lib/include
    ${tree-sitter-cpp_SOURCE_DIR}/src
    ${tree-sitter-python_SOURCE_DIR}/src
    ${tree-sitter-javascript_SOURCE_DIR}/src
    ${tree-sitter-typescript_SOURCE_DIR}/typescript/src
    ${tree-sitter-go_SOURCE_DIR}/src
    ${tree-sitter-rust_SOURCE_DIR}/src
    ${tree-sitter-java_SOURCE_DIR}/src
    ${tree-sitter-ruby_SOURCE_DIR}/src
    ${tree-sitter-c-sharp_SOURCE_DIR}/src
)
target_link_libraries(tree-sitter-parsers PUBLIC tree-sitter)

# Extract version from plugin.json
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../.claude-plugin/plugin.json" PLUGIN_JSON)
string(REGEX MATCH "\"version\"[^:]*:[^\"]*\"([^\"]+)\"" _ ${PLUGIN_JSON})
set(CHITTA_VERSION "${CMAKE_MATCH_1}")
message(STATUS "cc-soul version: ${CHITTA_VERSION}")

# Generate version header
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/chitta/version.hpp.in"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/chitta/version.hpp"
    @ONLY
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output binaries to ../bin/ for cleaner plugin structure
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin)

# Options
option(CHITTA_BUILD_TESTS "Build tests" ON)
option(CHITTA_BUILD_RPC "Build RPC server executable" ON)
option(CHITTA_WITH_ONNX "Enable ONNX Runtime embeddings" ON)

# Header-only library
add_library(chitta INTERFACE)
target_include_directories(chitta INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Find threads
find_package(Threads REQUIRED)
target_link_libraries(chitta INTERFACE Threads::Threads roaring tree-sitter-parsers duckdb)

# nlohmann/json for RPC server
find_package(nlohmann_json CONFIG QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(chitta INTERFACE nlohmann_json::nlohmann_json)
    target_compile_definitions(chitta INTERFACE CHITTA_WITH_JSON=1)
    message(STATUS "nlohmann_json found via CONFIG")
else()
    # Try to find in conda environment
    if(DEFINED ENV{CONDA_PREFIX})
        set(JSON_INCLUDE_DIR "$ENV{CONDA_PREFIX}/include")
        if(EXISTS "${JSON_INCLUDE_DIR}/nlohmann/json.hpp")
            target_include_directories(chitta INTERFACE ${JSON_INCLUDE_DIR})
            target_compile_definitions(chitta INTERFACE CHITTA_WITH_JSON=1)
            message(STATUS "nlohmann_json found at ${JSON_INCLUDE_DIR}")
        endif()
    endif()
endif()

# ONNX Runtime (optional)
if(CHITTA_WITH_ONNX)
    # Try to find ONNX Runtime
    # Check conda environment first
    if(DEFINED ENV{CONDA_PREFIX})
        set(ONNXRUNTIME_INCLUDE_DIR "$ENV{CONDA_PREFIX}/include")
        set(ONNXRUNTIME_LIB_DIR "$ENV{CONDA_PREFIX}/lib")
    else()
        # Fallback to system paths or user-specified
        set(ONNXRUNTIME_INCLUDE_DIR "" CACHE PATH "ONNX Runtime include directory")
        set(ONNXRUNTIME_LIB_DIR "" CACHE PATH "ONNX Runtime library directory")
    endif()

    # Create imported target (platform-aware library extension)
    if(APPLE)
        set(ONNXRUNTIME_LIB "${ONNXRUNTIME_LIB_DIR}/libonnxruntime.dylib")
    else()
        set(ONNXRUNTIME_LIB "${ONNXRUNTIME_LIB_DIR}/libonnxruntime.so")
    endif()

    add_library(onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
    )

    # Add to chitta
    target_link_libraries(chitta INTERFACE onnxruntime)
    target_compile_definitions(chitta INTERFACE CHITTA_WITH_ONNX=1)

    message(STATUS "ONNX Runtime enabled")
    message(STATUS "  Include: ${ONNXRUNTIME_INCLUDE_DIR}")
    message(STATUS "  Library: ${ONNXRUNTIME_LIB_DIR}")
endif()

# Tests
if(CHITTA_BUILD_TESTS)
    enable_testing()

    # Old mmap-based tests (disabled - using DuckDB now)
    # add_executable(chitta_test src/test.cpp)
    # target_link_libraries(chitta_test PRIVATE chitta)
    # add_test(NAME chitta_test COMMAND chitta_test)

    # SimpleMind test (disabled - using DuckDBMind now)
    # add_executable(simple_test src/simple_test.cpp)
    # target_link_libraries(simple_test PRIVATE chitta)

    # DuckDB store test
    add_executable(duckdb_test src/duckdb_test.cpp)
    target_link_libraries(duckdb_test PRIVATE duckdb_store duckdb)
    add_test(NAME duckdb_test COMMAND duckdb_test)
endif()

# DuckDBStore library
add_library(duckdb_store STATIC src/duckdb_store.cpp)
target_link_libraries(duckdb_store PUBLIC chitta duckdb)
target_include_directories(duckdb_store PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# PostgresStore library (optional, for HPC multi-writer)
if(CHITTA_WITH_POSTGRES)
    add_library(postgres_store STATIC src/postgres_store.cpp)
    target_link_libraries(postgres_store PUBLIC chitta libpq)
    target_include_directories(postgres_store PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    )
    target_compile_definitions(postgres_store PUBLIC CHITTA_WITH_POSTGRES=1)
    message(STATUS "PostgresStore enabled")
endif()

# Daemon/admin tool (uses DuckDBMind or PostgresMind)
add_executable(chittad
    src/simple_cli.cpp
    src/socket_server.cpp
    src/socket_client.cpp
)
target_link_libraries(chittad PRIVATE chitta duckdb_store)
if(CHITTA_WITH_POSTGRES)
    target_link_libraries(chittad PRIVATE postgres_store)
    target_compile_definitions(chittad PRIVATE CHITTA_WITH_POSTGRES=1)
endif()

# CLI tool for memory operations (thin client + MCP server)
# Output as 'chitta' binary (target named chitta_tool to avoid library conflict)
if(CHITTA_BUILD_RPC)
    add_executable(chitta_tool
        src/rpc_server.cpp
        src/socket_client.cpp
    )
    set_target_properties(chitta_tool PROPERTIES OUTPUT_NAME chitta)
    target_link_libraries(chitta_tool PRIVATE chitta duckdb_store)
    if(TARGET onnx_yantra)
        target_link_libraries(chitta_tool PRIVATE onnx_yantra)
        target_compile_definitions(chitta_tool PRIVATE CHITTA_WITH_ONNX)
    endif()
endif()

# Migration tool (from SQLite soul.db)
find_package(SQLite3)
if(SQLite3_FOUND)
    add_executable(chitta_migrate src/migrate.cpp)
    target_link_libraries(chitta_migrate PRIVATE chitta SQLite::SQLite3)
    message(STATUS "SQLite3 found: ${SQLite3_VERSION}")
else()
    message(STATUS "SQLite3 not found, migration tool disabled")
endif()

# Import tool (from chitta files - incremental merge)
add_executable(chitta_import src/import.cpp)
target_link_libraries(chitta_import PRIVATE chitta)

# Install
install(DIRECTORY include/ DESTINATION include)
install(TARGETS chitta EXPORT chittaTargets)

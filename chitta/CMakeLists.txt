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

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Output binaries to ../bin/ for cleaner plugin structure
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../bin)

# Options
option(CHITTA_BUILD_TESTS "Build tests" ON)
option(CHITTA_BUILD_MCP "Build MCP server executable" ON)
option(CHITTA_WITH_ONNX "Enable ONNX Runtime embeddings" ON)

# Header-only library
add_library(chitta INTERFACE)
target_include_directories(chitta INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Find threads
find_package(Threads REQUIRED)
target_link_libraries(chitta INTERFACE Threads::Threads roaring)

# nlohmann/json for MCP server
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
    add_executable(chitta_test src/test.cpp)
    target_link_libraries(chitta_test PRIVATE chitta)
    add_test(NAME chitta_test COMMAND chitta_test)
endif()

# CLI tool for memory operations (thin client with socket support)
# Output as 'chitta' binary (target named chitta_tool to avoid library conflict)
if(CHITTA_BUILD_MCP)
    add_executable(chitta_tool
        src/mcp_server.cpp
        src/socket_client.cpp
    )
    set_target_properties(chitta_tool PROPERTIES OUTPUT_NAME chitta)
    target_link_libraries(chitta_tool PRIVATE chitta)
endif()

# CLI tool (includes socket server/client for daemon mode)
add_executable(chitta_cli
    src/cli.cpp
    src/socket_server.cpp
    src/socket_client.cpp
)
target_link_libraries(chitta_cli PRIVATE chitta)

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

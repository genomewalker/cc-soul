cmake_minimum_required(VERSION 3.14)
project(chitta VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(CHITTA_BUILD_TESTS "Build tests" ON)
option(CHITTA_BUILD_PYTHON "Build Python bindings" OFF)
option(CHITTA_BUILD_MCP "Build MCP server executable" ON)
option(CHITTA_WITH_ONNX "Enable ONNX Runtime embeddings" OFF)

# Header-only library
add_library(chitta INTERFACE)
target_include_directories(chitta INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Find threads
find_package(Threads REQUIRED)
target_link_libraries(chitta INTERFACE Threads::Threads)

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

    # Create imported target
    add_library(onnxruntime SHARED IMPORTED)
    set_target_properties(onnxruntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRUNTIME_LIB_DIR}/libonnxruntime.so"
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

# Python bindings (optional)
if(CHITTA_BUILD_PYTHON)
    find_package(pybind11 CONFIG)
    if(pybind11_FOUND)
        pybind11_add_module(_chitta src/python.cpp)
        target_link_libraries(_chitta PRIVATE chitta)
    endif()
endif()

# MCP server executable
if(CHITTA_BUILD_MCP)
    add_executable(chitta_mcp src/mcp_server.cpp)
    target_link_libraries(chitta_mcp PRIVATE chitta)
    # Static link to avoid glibc version issues
    if(NOT APPLE)
        target_link_options(chitta_mcp PRIVATE -static-libgcc -static-libstdc++)
    endif()
endif()

# Migration tool
find_package(SQLite3)
if(SQLite3_FOUND)
    add_executable(chitta_migrate src/migrate.cpp)
    target_link_libraries(chitta_migrate PRIVATE chitta SQLite::SQLite3)
    message(STATUS "SQLite3 found: ${SQLite3_VERSION}")
else()
    message(STATUS "SQLite3 not found, migration tool disabled")
endif()

# Install
install(DIRECTORY include/ DESTINATION include)
install(TARGETS chitta EXPORT chittaTargets)

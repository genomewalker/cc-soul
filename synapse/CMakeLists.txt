cmake_minimum_required(VERSION 3.14)
project(synapse VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(SYNAPSE_BUILD_TESTS "Build tests" ON)
option(SYNAPSE_BUILD_PYTHON "Build Python bindings" OFF)
option(SYNAPSE_BUILD_MCP "Build MCP server executable" ON)
option(SYNAPSE_WITH_ONNX "Enable ONNX Runtime embeddings" OFF)

# Header-only library
add_library(synapse INTERFACE)
target_include_directories(synapse INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

# Find threads
find_package(Threads REQUIRED)
target_link_libraries(synapse INTERFACE Threads::Threads)

# nlohmann/json for MCP server
find_package(nlohmann_json CONFIG QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(synapse INTERFACE nlohmann_json::nlohmann_json)
    target_compile_definitions(synapse INTERFACE SYNAPSE_WITH_JSON=1)
    message(STATUS "nlohmann_json found via CONFIG")
else()
    # Try to find in conda environment
    if(DEFINED ENV{CONDA_PREFIX})
        set(JSON_INCLUDE_DIR "$ENV{CONDA_PREFIX}/include")
        if(EXISTS "${JSON_INCLUDE_DIR}/nlohmann/json.hpp")
            target_include_directories(synapse INTERFACE ${JSON_INCLUDE_DIR})
            target_compile_definitions(synapse INTERFACE SYNAPSE_WITH_JSON=1)
            message(STATUS "nlohmann_json found at ${JSON_INCLUDE_DIR}")
        endif()
    endif()
endif()

# ONNX Runtime (optional)
if(SYNAPSE_WITH_ONNX)
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

    # Add to synapse
    target_link_libraries(synapse INTERFACE onnxruntime)
    target_compile_definitions(synapse INTERFACE SYNAPSE_WITH_ONNX=1)

    message(STATUS "ONNX Runtime enabled")
    message(STATUS "  Include: ${ONNXRUNTIME_INCLUDE_DIR}")
    message(STATUS "  Library: ${ONNXRUNTIME_LIB_DIR}")
endif()

# Tests
if(SYNAPSE_BUILD_TESTS)
    enable_testing()
    add_executable(synapse_test src/test.cpp)
    target_link_libraries(synapse_test PRIVATE synapse)
    add_test(NAME synapse_test COMMAND synapse_test)
endif()

# Python bindings (optional)
if(SYNAPSE_BUILD_PYTHON)
    find_package(pybind11 CONFIG)
    if(pybind11_FOUND)
        pybind11_add_module(_synapse src/python.cpp)
        target_link_libraries(_synapse PRIVATE synapse)
    endif()
endif()

# MCP server executable
if(SYNAPSE_BUILD_MCP)
    add_executable(synapse_mcp src/mcp_server.cpp)
    target_link_libraries(synapse_mcp PRIVATE synapse)
endif()

# Migration tool
find_package(SQLite3)
if(SQLite3_FOUND)
    add_executable(synapse_migrate src/migrate.cpp)
    target_link_libraries(synapse_migrate PRIVATE synapse SQLite::SQLite3)
    message(STATUS "SQLite3 found: ${SQLite3_VERSION}")
else()
    message(STATUS "SQLite3 not found, migration tool disabled")
endif()

# Install
install(DIRECTORY include/ DESTINATION include)
install(TARGETS synapse EXPORT synapseTargets)

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

# Tree-sitter Swift grammar (alex-pinkus maintained fork)
FetchContent_Declare(
    tree-sitter-swift
    GIT_REPOSITORY https://github.com/alex-pinkus/tree-sitter-swift.git
    GIT_TAG 0.7.1-with-generated-files
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tree-sitter-swift)
if(NOT tree-sitter-swift_POPULATED)
    FetchContent_Populate(tree-sitter-swift)
endif()

FetchContent_Declare(
    tree-sitter-lua
    GIT_REPOSITORY https://github.com/tree-sitter-grammars/tree-sitter-lua.git
    GIT_TAG v0.2.0
    GIT_SHALLOW TRUE
)
FetchContent_GetProperties(tree-sitter-lua)
if(NOT tree-sitter-lua_POPULATED)
    FetchContent_Populate(tree-sitter-lua)
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
    ${tree-sitter-swift_SOURCE_DIR}/src/parser.c
    ${tree-sitter-swift_SOURCE_DIR}/src/scanner.c
    ${tree-sitter-lua_SOURCE_DIR}/src/parser.c
    ${tree-sitter-lua_SOURCE_DIR}/src/scanner.c
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
    ${tree-sitter-swift_SOURCE_DIR}/src
    ${tree-sitter-lua_SOURCE_DIR}/src
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
target_link_libraries(chitta INTERFACE Threads::Threads roaring tree-sitter-parsers)

# nlohmann/json for RPC server
find_package(nlohmann_json CONFIG QUIET)
if(nlohmann_json_FOUND)
    target_link_libraries(chitta INTERFACE nlohmann_json::nlohmann_json)
    target_compile_definitions(chitta INTERFACE CHITTA_WITH_JSON=1)
    message(STATUS "nlohmann_json found via CONFIG")
else()
    # Check user-specified path first, then conda
    if(NOT JSON_INCLUDE_DIR AND DEFINED ENV{CONDA_PREFIX})
        set(JSON_INCLUDE_DIR "$ENV{CONDA_PREFIX}/include")
    endif()
    if(JSON_INCLUDE_DIR AND EXISTS "${JSON_INCLUDE_DIR}/nlohmann/json.hpp")
        target_include_directories(chitta INTERFACE ${JSON_INCLUDE_DIR})
        target_compile_definitions(chitta INTERFACE CHITTA_WITH_JSON=1)
        message(STATUS "nlohmann_json found at ${JSON_INCLUDE_DIR}")
    else()
        # Download via CPM as last resort
        CPMAddPackage(
            NAME nlohmann_json
            VERSION 3.11.3
            GITHUB_REPOSITORY nlohmann/json
            OPTIONS "JSON_BuildTests OFF"
        )
        target_link_libraries(chitta INTERFACE nlohmann_json::nlohmann_json)
        target_compile_definitions(chitta INTERFACE CHITTA_WITH_JSON=1)
        message(STATUS "nlohmann_json downloaded via CPM")
    endif()
endif()

# ONNX Runtime (optional)
if(CHITTA_WITH_ONNX)
    # Auto-discover ONNX Runtime - no hardcoded paths
    # Priority: user -D flags > find_library/find_path

    if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIB_DIR)
        # Build search hints from environment
        set(_ort_search_paths "")

        # Official cc-soul deps location (has CoreML on macOS)
        if(EXISTS "$ENV{HOME}/.claude/deps/onnxruntime")
            list(APPEND _ort_search_paths "$ENV{HOME}/.claude/deps/onnxruntime")
        endif()

        # Environment-based paths (conda, virtualenv, etc.)
        if(DEFINED ENV{CONDA_PREFIX})
            list(APPEND _ort_search_paths "$ENV{CONDA_PREFIX}")
        endif()
        if(DEFINED ENV{VIRTUAL_ENV})
            list(APPEND _ort_search_paths "$ENV{VIRTUAL_ENV}")
        endif()

        # Homebrew (detect dynamically)
        if(APPLE)
            execute_process(
                COMMAND brew --prefix onnxruntime
                OUTPUT_VARIABLE _brew_ort_prefix
                OUTPUT_STRIP_TRAILING_WHITESPACE
                ERROR_QUIET
                RESULT_VARIABLE _brew_result
            )
            if(_brew_result EQUAL 0 AND EXISTS "${_brew_ort_prefix}")
                list(APPEND _ort_search_paths "${_brew_ort_prefix}")
            endif()
        endif()

        # System paths
        list(APPEND _ort_search_paths "/usr/local" "/usr")

        # Find the library first
        find_library(ONNXRUNTIME_LIBRARY
            NAMES onnxruntime
            HINTS ${_ort_search_paths}
            PATH_SUFFIXES lib lib64
        )

        if(ONNXRUNTIME_LIBRARY)
            get_filename_component(ONNXRUNTIME_LIB_DIR "${ONNXRUNTIME_LIBRARY}" DIRECTORY)
            get_filename_component(_ort_root "${ONNXRUNTIME_LIB_DIR}" DIRECTORY)

            # Find header - check both layouts (flat and nested)
            # Nested: onnxruntime/core/session/onnxruntime_cxx_api.h
            # Flat: onnxruntime_cxx_api.h
            find_path(ONNXRUNTIME_INCLUDE_DIR
                NAMES onnxruntime_cxx_api.h
                HINTS "${_ort_root}/include" ${_ort_search_paths}
                PATH_SUFFIXES
                    include
                    include/onnxruntime/core/session
                    onnxruntime/core/session
            )

            if(ONNXRUNTIME_INCLUDE_DIR)
                message(STATUS "ONNX Runtime: auto-detected")
                message(STATUS "  Library: ${ONNXRUNTIME_LIBRARY}")
                message(STATUS "  Include: ${ONNXRUNTIME_INCLUDE_DIR}")

                # Check for CoreML support (macOS official release)
                if(APPLE AND EXISTS "${ONNXRUNTIME_INCLUDE_DIR}/coreml_provider_factory.h")
                    set(ONNXRUNTIME_HAS_COREML TRUE)
                    message(STATUS "  CoreML: available")
                endif()
            endif()
        endif()

        # Cache for user override
        if(NOT ONNXRUNTIME_INCLUDE_DIR)
            set(ONNXRUNTIME_INCLUDE_DIR "" CACHE PATH "ONNX Runtime include directory")
        endif()
        if(NOT ONNXRUNTIME_LIB_DIR)
            set(ONNXRUNTIME_LIB_DIR "" CACHE PATH "ONNX Runtime library directory")
        endif()
    else()
        # User provided both dirs via -D flags, find the library
        message(STATUS "ONNX Runtime: using user-provided paths")
        message(STATUS "  Include: ${ONNXRUNTIME_INCLUDE_DIR}")
        message(STATUS "  Lib dir: ${ONNXRUNTIME_LIB_DIR}")
        find_library(ONNXRUNTIME_LIBRARY
            NAMES onnxruntime
            HINTS ${ONNXRUNTIME_LIB_DIR}
            NO_DEFAULT_PATH
        )
        if(ONNXRUNTIME_LIBRARY)
            message(STATUS "  Library: ${ONNXRUNTIME_LIBRARY}")
        else()
            message(WARNING "ONNX Runtime library not found in ${ONNXRUNTIME_LIB_DIR}")
        endif()
    endif()

    # Create imported target if found
    if(ONNXRUNTIME_LIBRARY AND ONNXRUNTIME_INCLUDE_DIR)
        add_library(onnxruntime SHARED IMPORTED)
        set_target_properties(onnxruntime PROPERTIES
            IMPORTED_LOCATION "${ONNXRUNTIME_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${ONNXRUNTIME_INCLUDE_DIR}"
        )

        # Link Apple frameworks for CoreML EP
        if(ONNXRUNTIME_HAS_COREML)
            target_compile_definitions(chitta INTERFACE CHITTA_WITH_COREML=1)
        endif()

        # Add to chitta
        target_link_libraries(chitta INTERFACE onnxruntime)
        target_compile_definitions(chitta INTERFACE CHITTA_WITH_ONNX=1)
        message(STATUS "ONNX Runtime enabled")
    else()
        message(STATUS "ONNX Runtime: not found (embeddings disabled)")
    endif()
endif()


# chitta-field — required storage backend
# Resolve root: prefer CHITTA_FIELD_ROOT env/cmake var, then sibling submodule, then legacy path
if(NOT CHITTA_FIELD_ROOT)
    # Submodule path: cc-soul/chitta-field (one level up from chitta/)
    set(_CF_SUBMODULE "${CMAKE_CURRENT_SOURCE_DIR}/../chitta-field")
    if(EXISTS "${_CF_SUBMODULE}/Cargo.toml")
        set(CHITTA_FIELD_ROOT "${_CF_SUBMODULE}")
    else()
        message(FATAL_ERROR "chitta-field submodule not found at ${_CF_SUBMODULE}.\n"
            "Run: git submodule update --init chitta-field && cd chitta-field && ./build.sh\n"
            "Or set -DCHITTA_FIELD_ROOT=/path/to/chitta-field on the cmake command line.")
    endif()
endif()
set(CHITTA_FIELD_LIB "${CHITTA_FIELD_ROOT}/target/release/libchitta_field.a")
set(CHITTA_FIELD_INCLUDE "${CHITTA_FIELD_ROOT}/include")

if(NOT EXISTS "${CHITTA_FIELD_LIB}")
    message(FATAL_ERROR "chitta-field lib not found at ${CHITTA_FIELD_LIB} — build it first: cd chitta-field && ./build.sh")
endif()
message(STATUS "chitta-field: ${CHITTA_FIELD_LIB}")

# rtk — token-optimized command filter (submodule at cc-soul/rtk)
set(_RTK_SUBMODULE "${CMAKE_CURRENT_SOURCE_DIR}/../rtk")
set(_RTK_BIN "${CMAKE_CURRENT_SOURCE_DIR}/../bin/rtk")
if(EXISTS "${_RTK_SUBMODULE}/Cargo.toml")
    # Prefer rustup's cargo over conda/system cargo
    execute_process(COMMAND rustup which cargo OUTPUT_VARIABLE _RUSTUP_CARGO OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_RUSTUP_CARGO)
        set(CARGO_EXECUTABLE "${_RUSTUP_CARGO}")
    else()
        find_program(CARGO_EXECUTABLE HINTS "$ENV{HOME}/.cargo/bin" NAMES cargo)
    endif()
    if(CARGO_EXECUTABLE)
        # Read toolchain from rust-toolchain.toml if present, fall back to 1.92.0
        set(_RTK_TOOLCHAIN "1.92.0")
        if(EXISTS "${_RTK_SUBMODULE}/rust-toolchain.toml")
            file(STRINGS "${_RTK_SUBMODULE}/rust-toolchain.toml" _TC_LINE REGEX "channel = ")
            if(_TC_LINE)
                string(REGEX REPLACE ".*channel = \"([^\"]+)\".*" "\\1" _RTK_TOOLCHAIN "${_TC_LINE}")
            endif()
        endif()
        get_filename_component(_CARGO_DIR "${CARGO_EXECUTABLE}" DIRECTORY)
        set(_RUSTC_EXECUTABLE "${_CARGO_DIR}/rustc")
        add_custom_command(
            OUTPUT "${_RTK_BIN}"
            COMMAND ${CMAKE_COMMAND} -E env
                "RUSTUP_TOOLCHAIN=${_RTK_TOOLCHAIN}"
                "RUSTC=${_RUSTC_EXECUTABLE}"
                "${CARGO_EXECUTABLE}" build --release --manifest-path "${_RTK_SUBMODULE}/Cargo.toml"
            COMMAND ${CMAKE_COMMAND} -E copy "${_RTK_SUBMODULE}/target/release/rtk" "${_RTK_BIN}"
            DEPENDS "${_RTK_SUBMODULE}/src"
            COMMENT "Building rtk token filter"
        )
        add_custom_target(rtk ALL DEPENDS "${_RTK_BIN}")
        message(STATUS "rtk: will build from ${_RTK_SUBMODULE}")
    else()
        message(STATUS "rtk: cargo not found, skipping")
    endif()
else()
    message(STATUS "rtk: submodule not found at ${_RTK_SUBMODULE}, skipping")
endif()

# Daemon/admin tool
add_executable(chittad
    src/simple_cli.cpp
    src/queue_processor.cpp
    src/daemon_lifecycle.cpp
    src/distillation.cpp
    src/socket_server.cpp
    src/socket_client.cpp
    src/subconscious.cpp
    src/soul_controller.cpp
    src/transcript_parser.cpp
    src/ssl_parser.cpp
    src/native_distiller.cpp
    src/ingester.cpp
    src/wiki_export.cpp
    src/embedding_export.cpp
    src/temporal.cpp
    src/query_intent.cpp
    src/sadhana/brain_provider.cpp
    src/sadhana/sadhana_manager.cpp
)
target_include_directories(chittad PRIVATE "${CHITTA_FIELD_INCLUDE}")
find_package(Python3 REQUIRED COMPONENTS Development)
target_link_libraries(chittad PRIVATE chitta "${CHITTA_FIELD_LIB}" dl pthread Python3::Python)
list(APPEND CHITTA_RUNTIME_LIBS "${Python3_LIBRARY_DIRS}")
target_compile_definitions(chittad PRIVATE CHITTA_FIELD_AVAILABLE=1)

# CLI tool for memory operations (thin client + MCP server)
# Output as 'chitta' binary (target named chitta_tool to avoid library conflict)
if(CHITTA_BUILD_RPC)
    add_executable(chitta_tool
        src/rpc_server.cpp
        src/socket_client.cpp
    )
    set_target_properties(chitta_tool PROPERTIES OUTPUT_NAME chitta)
    target_link_libraries(chitta_tool PRIVATE chitta)
    target_include_directories(chitta_tool PRIVATE "${CHITTA_FIELD_INCLUDE}")
    if(TARGET onnx_yantra)
        target_link_libraries(chitta_tool PRIVATE onnx_yantra)
        target_compile_definitions(chitta_tool PRIVATE CHITTA_WITH_ONNX)
    endif()
endif()

# Thinking block insight extractor — perception-change mining from session JSONL
add_executable(chitta_thinking src/thinking_extract.cpp src/socket_client.cpp)
target_link_libraries(chitta_thinking PRIVATE chitta)
target_include_directories(chitta_thinking PRIVATE "${CHITTA_FIELD_INCLUDE}")
install(TARGETS chitta_thinking DESTINATION bin)

# Unified migration tool: chitta_migrate
# Handles all source formats (DuckDB, soul.db) with auto-detection
add_executable(chitta_migrate src/migrate.cpp)
target_link_libraries(chitta_migrate PRIVATE chitta)
target_include_directories(chitta_migrate PRIVATE "${CHITTA_FIELD_INCLUDE}")
install(TARGETS chitta_migrate DESTINATION bin)

# RPATH configuration for runtime library discovery
# CI builds can override with CMAKE_INSTALL_RPATH=$ORIGIN to bundle libs
set(CHITTA_RUNTIME_LIBS "")


# Get ONNX Runtime library directory if enabled
if(CHITTA_WITH_ONNX AND ONNXRUNTIME_LIB_DIR)
    list(APPEND CHITTA_RUNTIME_LIBS "${ONNXRUNTIME_LIB_DIR}")
endif()

# Remove duplicates
list(REMOVE_DUPLICATES CHITTA_RUNTIME_LIBS)

# Set RPATH for all executables
# If CMAKE_INSTALL_RPATH is set externally (e.g., $ORIGIN for bundled builds), use it
# Otherwise, use the discovered library paths
if(CMAKE_INSTALL_RPATH)
    set(CHITTA_RPATH "${CMAKE_INSTALL_RPATH}")
    message(STATUS "Using externally specified RPATH: ${CHITTA_RPATH}")
else()
    string(JOIN ":" CHITTA_RPATH ${CHITTA_RUNTIME_LIBS})
    message(STATUS "Using discovered library RPATH: ${CHITTA_RPATH}")
endif()

# Apply RPATH to all executables
foreach(exe chittad)
    if(TARGET ${exe})
        set_target_properties(${exe} PROPERTIES
            BUILD_RPATH "${CHITTA_RPATH}"
            INSTALL_RPATH "${CHITTA_RPATH}"
        )
    endif()
endforeach()

if(TARGET chitta_tool)
    set_target_properties(chitta_tool PROPERTIES
        BUILD_RPATH "${CHITTA_RPATH}"
        INSTALL_RPATH "${CHITTA_RPATH}"
    )
endif()


# Install
install(DIRECTORY include/ DESTINATION include)
install(TARGETS chitta EXPORT chittaTargets)

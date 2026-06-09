cmake_minimum_required(VERSION 3.16)
project(RuntimeBenchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Build options (ON = auto-detect, OFF = force-disable)
option(BUILD_ICEORYX2_APP "Build iceoryx2 app (auto-detected if ON)" ON)
option(BUILD_ZENOH_APP    "Build Zenoh app (auto-detected if ON)"    ON)

# Find Protobuf (required for serialization)
find_package(Protobuf REQUIRED)

# Fetch RapidJSON (header-only library, skip its CMakeLists.txt)
include(FetchContent)
FetchContent_Declare(
    rapidjson
    GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
    GIT_TAG master
    SOURCE_SUBDIR nonexistent
)
FetchContent_MakeAvailable(rapidjson)

# Create interface library for RapidJSON headers
add_library(rapidjson INTERFACE)
target_include_directories(rapidjson INTERFACE ${rapidjson_SOURCE_DIR}/include)

# Fetch toml++ (header-only library)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

# Google Test
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)
enable_testing()

# Core library (no transport dependencies)
add_subdirectory(src/rybench)

# Tests
add_subdirectory(tests)

# Filewriter app (always built — no external dependencies)
add_subdirectory(apps/rybench-filewriter)

# Launcher (always built — no transport dependencies)
add_subdirectory(apps/rybench-launch)

# Zenoh app — enabled if BUILD_ZENOH_APP=ON and zenohcxx is found
if(BUILD_ZENOH_APP)
    find_package(zenohc   QUIET)
    find_package(zenohcxx QUIET)

    if(zenohcxx_FOUND)
        add_subdirectory(apps/rybench-zenoh)
        message(STATUS "Zenoh app: ENABLED")
    else()
        message(STATUS "Zenoh app: DISABLED (zenohcxx not found)")
    endif()
endif()

# Iceoryx2 app — enabled if BUILD_ICEORYX2_APP=ON and artifacts are found
set(ICEORYX2_ROOT "$ENV{HOME}/Projects/third-party/iceoryx2")
set(ICEORYX2_ARTIFACT_PATH "${ICEORYX2_ROOT}/target/release"
    CACHE PATH "Path to pre-built iceoryx2 Rust artifacts")

if(BUILD_ICEORYX2_APP)
    if(EXISTS "${ICEORYX2_ARTIFACT_PATH}/libiceoryx2_ffi_c.a")
        set(RUST_BUILD_ARTIFACT_PATH ${ICEORYX2_ARTIFACT_PATH} CACHE PATH "" FORCE)
        add_subdirectory(
            ${ICEORYX2_ROOT}
            ${CMAKE_BINARY_DIR}/_deps/iceoryx2-build
        )
        if(TARGET static-lib-cxx AND NOT TARGET Iceoryx2::cxx)
            add_library(Iceoryx2::cxx ALIAS static-lib-cxx)
        endif()
        add_subdirectory(apps/rybench-iceoryx2)
        message(STATUS "Iceoryx2 app: ENABLED")
    else()
        message(STATUS "Iceoryx2 app: DISABLED (artifacts not found at ${ICEORYX2_ARTIFACT_PATH})")
    endif()
endif()

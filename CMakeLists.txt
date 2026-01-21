cmake_minimum_required(VERSION 3.16)
project(RuntimeBenchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Build options
option(BUILD_ICEORYX2_APP "Build Iceoryx2 publisher app" ON)
option(BUILD_ZENOH_APP "Build Zenoh publisher app" ON)

# Find Protobuf (required for serialization)
find_package(Protobuf REQUIRED)

# Fetch RapidJSON (header-only library)
include(FetchContent)
FetchContent_Declare(
    rapidjson
    GIT_REPOSITORY https://github.com/Tencent/rapidjson.git
    GIT_TAG master
)
FetchContent_GetProperties(rapidjson)
if(NOT rapidjson_POPULATED)
    FetchContent_Populate(rapidjson)
endif()

# Create interface library for RapidJSON (header-only, skip its CMakeLists.txt)
add_library(rapidjson INTERFACE)
target_include_directories(rapidjson INTERFACE ${rapidjson_SOURCE_DIR}/include)

# Fetch toml++ (header-only library)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG v3.4.0
)
FetchContent_MakeAvailable(tomlplusplus)

# Core library (no transport dependencies)
add_subdirectory(src/rybench)

# Filewriter app (always built - no external dependencies)
add_subdirectory(src/rybench-filewriter)

# Zenoh app
if(BUILD_ZENOH_APP)
    find_package(zenohc QUIET)
    if(zenohc_FOUND)
        find_package(zenohcxx QUIET)
    endif()

    if(zenohcxx_FOUND)
        add_subdirectory(src/rybench-zenoh)
        message(STATUS "Zenoh app: ENABLED")
    else()
        message(STATUS "Zenoh app: DISABLED (zenohcxx not found)")
    endif()
endif()

# Iceoryx2 app
# todo: move iceroxy2 app to project directory
set(ICEORYX2_ROOT "$ENV{HOME}/Projects/third-party/iceoryx2")
if(BUILD_ICEORYX2_APP)
    add_subdirectory(
        ${ICEORYX2_ROOT}
        ${CMAKE_BINARY_DIR}/_deps/iceoryx2-build
    )

    # Create alias for consistent naming
    # Use static-lib-cxx which includes the Rust FFI library
    if(TARGET static-lib-cxx AND NOT TARGET Iceoryx2::cxx)
        add_library(Iceoryx2::cxx ALIAS static-lib-cxx)
    endif()

    add_subdirectory(src/rybench-iceoryx2)
    message(STATUS "Iceoryx2 app: ENABLED")
endif()


# Test consumers
add_subdirectory(test)

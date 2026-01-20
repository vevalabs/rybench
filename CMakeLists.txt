cmake_minimum_required(VERSION 3.16)
project(RuntimeBenchmark LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Find iceoryx
find_package(iceoryx_posh REQUIRED)
find_package(iceoryx_hoofs REQUIRED)

# Find Protobuf
find_package(Protobuf REQUIRED)

# Find Zenoh (zenohc must be found first as zenohcxx depends on it)
find_package(zenohc QUIET)
if(zenohc_FOUND)
    find_package(zenohcxx QUIET)
endif()

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

# Add subdirectories
add_subdirectory(src/common)
add_subdirectory(src/serializer)
add_subdirectory(src/generator)
add_subdirectory(src/publisher)
add_subdirectory(src/rybench)
add_subdirectory(test)

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

# Fetch nlohmann/json
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Add subdirectories
add_subdirectory(src/common)
add_subdirectory(src/serializer)
add_subdirectory(src/generator)
add_subdirectory(src/publisher)
add_subdirectory(src/rybench)
add_subdirectory(test)

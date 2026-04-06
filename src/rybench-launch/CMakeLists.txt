add_executable(rybench-launch main.cpp)

target_include_directories(rybench-launch PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rybench/common/include
)

target_link_libraries(rybench-launch PRIVATE tomlplusplus::tomlplusplus)

set_target_properties(rybench-launch PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

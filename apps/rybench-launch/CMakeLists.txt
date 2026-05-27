add_executable(rybench-launch
    main.cpp
    launcher.cpp
)

target_include_directories(rybench-launch PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(rybench-launch PRIVATE tomlplusplus::tomlplusplus)

set_target_properties(rybench-launch PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

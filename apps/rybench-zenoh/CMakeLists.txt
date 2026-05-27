add_executable(rybench-zenoh
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/zenoh/zenoh_publisher.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/zenoh/zenoh_consumer.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/zenoh/zenoh_runner.cpp
)

target_include_directories(rybench-zenoh PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/zenoh
)

target_link_libraries(rybench-zenoh PRIVATE
    -Wl,--whole-archive rybench -Wl,--no-whole-archive
    zenohcxx::zenohc
)

set_target_properties(rybench-zenoh PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_executable(rybench-zenoh
    main.cpp
    zenoh_publisher.cpp
    zenoh_consumer.cpp
)

target_include_directories(rybench-zenoh PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(rybench-zenoh PRIVATE
    -Wl,--whole-archive rybench -Wl,--no-whole-archive
    zenohcxx::zenohc
)

set_target_properties(rybench-zenoh PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

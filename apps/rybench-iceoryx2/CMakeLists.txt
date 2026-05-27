add_executable(rybench-iceoryx2
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/iceoryx2/iceoryx2_publisher.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/iceoryx2/iceoryx2_consumer.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/iceoryx2/iceoryx2_runner.cpp
)

target_include_directories(rybench-iceoryx2 PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/iceoryx2
)

target_link_libraries(rybench-iceoryx2 PRIVATE rybench Iceoryx2::cxx)

set_target_properties(rybench-iceoryx2 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

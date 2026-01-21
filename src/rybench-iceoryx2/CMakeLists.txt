add_executable(rybench-iceoryx2
    main.cpp
    iceoryx2_publisher.cpp
)

target_include_directories(rybench-iceoryx2 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(rybench-iceoryx2 PRIVATE
    rybench
    Iceoryx2::cxx
)

set_target_properties(rybench-iceoryx2 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)
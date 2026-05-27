add_executable(rybench-filewriter
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/filewriter/filewriter_publisher.cpp
)

target_include_directories(rybench-filewriter PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/filewriter
)

target_link_libraries(rybench-filewriter PRIVATE rybench)

set_target_properties(rybench-filewriter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

add_executable(rybench-filewriter
    main.cpp
)

target_link_libraries(rybench-filewriter PRIVATE
    rybench
)

set_target_properties(rybench-filewriter PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

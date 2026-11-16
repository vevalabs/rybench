add_executable(monpoly-echo main.cpp)

set_target_properties(monpoly-echo PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Reelay monitoring harness: decode a serialized MTL dump and feed a past-time
# Reelay monitor. Reelay is header-only but pulls vendored Boost + CUDD from its
# own build tree (rv-tools/reelay/build/_deps), so we reference those directly.
set(REELAY_ROOT   ${CMAKE_SOURCE_DIR}/rv-tools/reelay)
set(REELAY_DEPS   ${REELAY_ROOT}/build/_deps)
file(GLOB REELAY_BOOST_INC ${REELAY_DEPS}/boost-src/libs/*/include)

add_executable(rybench-monitor main.cpp)

target_include_directories(rybench-monitor PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rybench/monitor                  # monitor_sink.hpp
    ${CMAKE_SOURCE_DIR}/src/rybench/serializer/flatbuffers   # flatbuffers_schema.hpp
    ${CMAKE_SOURCE_DIR}/src/rybench/serializer/protobuf      # protobuf_serializer.hpp
    ${CMAKE_BINARY_DIR}/_deps/rapidjson-src/include          # rapidjson (fetched by rybench)
    ${REELAY_ROOT}/include
    ${REELAY_DEPS}/cudd-src/include
    ${REELAY_BOOST_INC}
)

target_link_libraries(rybench-monitor PRIVATE
    rybench
    flatbuffers
    ${REELAY_DEPS}/cudd-build/libcudd.a
    pthread
)

set_target_properties(rybench-monitor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

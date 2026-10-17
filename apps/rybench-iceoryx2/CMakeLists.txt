add_executable(rybench-iceoryx2
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/iceoryx2/iceoryx2_publisher.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/iceoryx2/iceoryx2_consumer.cpp
)

# Reelay online monitor consumer (via MonitorSink) — vendored Boost + CUDD from
# reelay's own build tree (same recipe as apps/rybench-monitor).
set(REELAY_ROOT ${CMAKE_SOURCE_DIR}/rv-tools/reelay)
set(REELAY_DEPS ${REELAY_ROOT}/build/_deps)
file(GLOB REELAY_BOOST_INC ${REELAY_DEPS}/boost-src/libs/*/include)

target_include_directories(rybench-iceoryx2 PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/iceoryx2
    ${CMAKE_SOURCE_DIR}/src/rybench/monitor                  # monitor_sink.hpp
    ${CMAKE_SOURCE_DIR}/src/rybench/serializer/flatbuffers
    ${CMAKE_SOURCE_DIR}/src/rybench/serializer/protobuf
    ${CMAKE_BINARY_DIR}/_deps/rapidjson-src/include
    ${REELAY_ROOT}/include
    ${REELAY_DEPS}/cudd-src/include
    ${REELAY_BOOST_INC}
)

target_link_libraries(rybench-iceoryx2 PRIVATE
    rybench Iceoryx2::cxx flatbuffers
    ${REELAY_DEPS}/cudd-build/libcudd.a
    pthread
)

set_target_properties(rybench-iceoryx2 PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

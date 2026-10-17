add_executable(rybench-zenoh
    main.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/zenoh/zenoh_publisher.cpp
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/zenoh/zenoh_consumer.cpp
)

# Reelay online monitor consumer (via MonitorSink) — vendored Boost + CUDD.
set(REELAY_ROOT ${CMAKE_SOURCE_DIR}/rv-tools/reelay)
set(REELAY_DEPS ${REELAY_ROOT}/build/_deps)
file(GLOB REELAY_BOOST_INC ${REELAY_DEPS}/boost-src/libs/*/include)

target_include_directories(rybench-zenoh PRIVATE
    ${CMAKE_SOURCE_DIR}/src/rybench/transport/zenoh
    ${CMAKE_SOURCE_DIR}/src/rybench/monitor
    ${CMAKE_SOURCE_DIR}/src/rybench/serializer/flatbuffers
    ${CMAKE_SOURCE_DIR}/src/rybench/serializer/protobuf
    ${CMAKE_BINARY_DIR}/_deps/rapidjson-src/include
    ${REELAY_ROOT}/include
    ${REELAY_DEPS}/cudd-src/include
    ${REELAY_BOOST_INC}
)

target_link_libraries(rybench-zenoh PRIVATE
    -Wl,--whole-archive rybench -Wl,--no-whole-archive
    zenohcxx::zenohc
    flatbuffers
    ${REELAY_DEPS}/cudd-build/libcudd.a
    pthread
)

set_target_properties(rybench-zenoh PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

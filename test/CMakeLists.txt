# Iceoryx2 consumers (only if Iceoryx2 is available)
if(BUILD_ICEORYX2_APP AND TARGET Iceoryx2::cxx)
    add_executable(iceoryx2_consumer_json
        iceoryx2_consumer_json.cpp
    )

    add_executable(iceoryx2_consumer_protobuf
        iceoryx2_consumer_protobuf.cpp
    )

    add_executable(iceoryx2_consumer_flatbuffers
        iceoryx2_consumer_flatbuffers.cpp
    )

    target_link_libraries(iceoryx2_consumer_json PRIVATE
        Iceoryx2::cxx
    )

    target_link_libraries(iceoryx2_consumer_protobuf PRIVATE
        Iceoryx2::cxx
        rybench
    )

    target_link_libraries(iceoryx2_consumer_flatbuffers PRIVATE
        Iceoryx2::cxx
        rybench
        rapidjson
    )

    # Include generated headers (same as Zenoh)
    target_include_directories(iceoryx2_consumer_protobuf PRIVATE
        ${CMAKE_BINARY_DIR}/src/rybench
    )

    target_include_directories(iceoryx2_consumer_flatbuffers PRIVATE
        ${CMAKE_BINARY_DIR}/src/rybench
    )

    message(STATUS "Iceoryx2 consumers: ENABLED")
else()
    message(STATUS "Iceoryx2 consumers: DISABLED (iceoryx2 not available)")
endif()


# Zenoh consumers (only if Zenoh C++ bindings are available)
if(zenohcxx_FOUND)
    add_executable(zenoh_consumer_json
        zenoh_consumer_json.cpp
    )

    add_executable(zenoh_consumer_protobuf
        zenoh_consumer_protobuf.cpp
    )

    add_executable(zenoh_consumer_flatbuffers
        zenoh_consumer_flatbuffers.cpp
    )

    set(ZENOH_LIB zenohcxx::zenohc)

    target_link_libraries(zenoh_consumer_json PRIVATE
        ${ZENOH_LIB}
    )

    target_link_libraries(zenoh_consumer_protobuf PRIVATE
        ${ZENOH_LIB}
        rybench
    )

    target_link_libraries(zenoh_consumer_flatbuffers PRIVATE
        ${ZENOH_LIB}
        rybench
    )

    # Include generated headers
    target_include_directories(zenoh_consumer_protobuf PRIVATE
        ${CMAKE_BINARY_DIR}/src/rybench
    )

    target_include_directories(zenoh_consumer_flatbuffers PRIVATE
        ${CMAKE_BINARY_DIR}/src/rybench
    )
endif()

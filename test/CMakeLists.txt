add_executable(consumer_json
    consumer_json.cpp
)

target_link_libraries(consumer_json PRIVATE
    common
    publisher
    iceoryx_posh::iceoryx_posh
)

add_executable(consumer_protobuf
    consumer_protobuf.cpp
)

target_link_libraries(consumer_protobuf PRIVATE
    common
    publisher
    iceoryx_posh::iceoryx_posh
    protobuf::libprotobuf
)

target_include_directories(consumer_protobuf PRIVATE
    ${CMAKE_BINARY_DIR}/src/common
)

add_executable(consumer_flatbuffers
    consumer_flatbuffers.cpp
)

target_link_libraries(consumer_flatbuffers PRIVATE
    common
    publisher
    iceoryx_posh::iceoryx_posh
    flatbuffers
)

target_include_directories(consumer_flatbuffers PRIVATE
    ${CMAKE_BINARY_DIR}/src/common
)

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
        common
    )

    target_link_libraries(zenoh_consumer_flatbuffers PRIVATE
        ${ZENOH_LIB}
        common
    )

    # Include generated headers
    target_include_directories(zenoh_consumer_protobuf PRIVATE
        ${CMAKE_BINARY_DIR}/src/common
    )

    target_include_directories(zenoh_consumer_flatbuffers PRIVATE
        ${CMAKE_BINARY_DIR}/src/common
    )
endif()

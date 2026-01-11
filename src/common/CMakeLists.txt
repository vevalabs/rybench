# Generate protobuf sources
set(PROTO_FILES ${CMAKE_SOURCE_DIR}/config/protos/osi_sensorview.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Generate FlatBuffers headers and binary schemas
set(FBS_FILES
    ${CMAKE_SOURCE_DIR}/config/schemas/osi_sensorview.fbs
    ${CMAKE_SOURCE_DIR}/config/schemas/benchmark.fbs
)
set(FBS_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(FBS_RUNTIME_DIR ${CMAKE_BINARY_DIR}/config/schemas)

find_program(FLATC flatc REQUIRED)

# Create runtime config directory
file(MAKE_DIRECTORY ${FBS_RUNTIME_DIR})

foreach(FBS_FILE ${FBS_FILES})
    get_filename_component(FBS_NAME ${FBS_FILE} NAME_WE)
    set(FBS_HEADER ${FBS_OUTPUT_DIR}/${FBS_NAME}_generated.h)
    set(FBS_BFBS ${FBS_OUTPUT_DIR}/${FBS_NAME}.bfbs)

    # Generate C++ header
    add_custom_command(
        OUTPUT ${FBS_HEADER}
        COMMAND ${FLATC} --cpp -o ${FBS_OUTPUT_DIR} ${FBS_FILE}
        DEPENDS ${FBS_FILE}
        COMMENT "Generating FlatBuffers header for ${FBS_NAME}"
    )

    # Generate binary schema for reflection
    add_custom_command(
        OUTPUT ${FBS_BFBS}
        COMMAND ${FLATC} --binary --schema -o ${FBS_OUTPUT_DIR} ${FBS_FILE}
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${FBS_BFBS} ${FBS_RUNTIME_DIR}/${FBS_NAME}.bfbs
        DEPENDS ${FBS_FILE}
        COMMENT "Generating FlatBuffers binary schema for ${FBS_NAME}"
    )

    list(APPEND FBS_HDRS ${FBS_HEADER})
    list(APPEND FBS_BFBS_FILES ${FBS_BFBS})
endforeach()

add_library(common STATIC
    config.cpp
    schema.cpp
    recorder.cpp
    message.cpp
    flatbuffers_schema.cpp
    ${PROTO_SRCS}
    ${FBS_HDRS}
    ${FBS_BFBS_FILES}
)

target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(common PUBLIC
    nlohmann_json::nlohmann_json
    iceoryx_posh::iceoryx_posh
    iceoryx_hoofs::iceoryx_hoofs
    ${Protobuf_LIBRARIES}
    flatbuffers
)

# Generate protobuf sources
set(PROTO_FILES ${CMAKE_SOURCE_DIR}/config/protos/osi_sensorview.proto)
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# Generate FlatBuffers headers
set(FBS_FILES ${CMAKE_SOURCE_DIR}/config/flatbuffers/osi_sensorview.fbs)
set(FBS_OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR})

find_program(FLATC flatc REQUIRED)

foreach(FBS_FILE ${FBS_FILES})
    get_filename_component(FBS_NAME ${FBS_FILE} NAME_WE)
    set(FBS_HEADER ${FBS_OUTPUT_DIR}/${FBS_NAME}_generated.h)
    add_custom_command(
        OUTPUT ${FBS_HEADER}
        COMMAND ${FLATC} --cpp -o ${FBS_OUTPUT_DIR} ${FBS_FILE}
        DEPENDS ${FBS_FILE}
        COMMENT "Generating FlatBuffers header for ${FBS_NAME}"
    )
    list(APPEND FBS_HDRS ${FBS_HEADER})
endforeach()

add_library(common STATIC
    ${PROTO_SRCS}
    ${FBS_HDRS}
)

target_include_directories(common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
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

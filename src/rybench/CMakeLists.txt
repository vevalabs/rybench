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

# librybench - core library (no transport dependencies)
add_library(rybench STATIC
    # Common
    common/config.cpp
    common/message.cpp
    common/flatbuffers_schema.cpp
    # Generator
    generator/fixed/fixed_generator.cpp
    generator/schema_guided/schema_guided_generator.cpp
    generator/property_guided/property_guided_generator.cpp
    generator/generator_factory.cpp
    generator/reflection_builder.cpp
    # Serializer
    serializer/json/json_serializer.cpp
    serializer/protobuf/protobuf_serializer.cpp
    serializer/flatbuffers/flatbuffers_serializer.cpp
    serializer/serializer_factory.cpp
    # Generated sources
    ${FBS_HDRS}
    ${FBS_BFBS_FILES}
)

target_include_directories(rybench PUBLIC
    # Common
    ${CMAKE_CURRENT_SOURCE_DIR}/common/include
    # Generator
    ${CMAKE_CURRENT_SOURCE_DIR}/generator/include
    ${CMAKE_CURRENT_SOURCE_DIR}/generator/fixed/include
    ${CMAKE_CURRENT_SOURCE_DIR}/generator/schema_guided/include
    ${CMAKE_CURRENT_SOURCE_DIR}/generator/property_guided/include
    # Serializer
    ${CMAKE_CURRENT_SOURCE_DIR}/serializer/include
    ${CMAKE_CURRENT_SOURCE_DIR}/serializer/json/include
    ${CMAKE_CURRENT_SOURCE_DIR}/serializer/protobuf/include
    ${CMAKE_CURRENT_SOURCE_DIR}/serializer/flatbuffers/include
    # Generated headers
    ${CMAKE_CURRENT_BINARY_DIR}
    ${Protobuf_INCLUDE_DIRS}
)

target_link_libraries(rybench PUBLIC
    rapidjson
    tomlplusplus::tomlplusplus
    ${Protobuf_LIBRARIES}
    flatbuffers
)

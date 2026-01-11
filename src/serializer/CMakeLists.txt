add_library(serializer STATIC
    json/json_serializer.cpp
    protobuf/protobuf_serializer.cpp
    flatbuffers/flatbuffers_serializer.cpp
    serializer_factory.cpp
)

target_include_directories(serializer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/json/include
    ${CMAKE_CURRENT_SOURCE_DIR}/protobuf/include
    ${CMAKE_CURRENT_SOURCE_DIR}/flatbuffers/include
)

target_link_libraries(serializer PUBLIC
    common
)

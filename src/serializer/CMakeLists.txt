add_library(serializer STATIC
    protobuf_serializer.cpp
    flatbuffers_serializer.cpp
    messages/osi_sensorview_pb.cpp
    messages/osi_sensorview_fbs.cpp
)

target_include_directories(serializer PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/..
)

target_link_libraries(serializer PUBLIC
    common
)

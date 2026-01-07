#include "serializer/protobuf_serializer.hpp"
#include "osi_sensorview.pb.h"

// Register OSI protobuf messages at static initialization
REGISTER_PROTO_MESSAGE(osi::SensorViewMessage, OsiSensorViewMessage)
REGISTER_PROTO_MESSAGE(osi::MovingObject, OsiMovingObject)
REGISTER_PROTO_MESSAGE(osi::Timestamp, OsiTimestamp)
REGISTER_PROTO_MESSAGE(osi::Vector3d, OsiVector3d)

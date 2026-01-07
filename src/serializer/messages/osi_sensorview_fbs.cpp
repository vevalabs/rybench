#include "serializer/flatbuffers_serializer.hpp"
#include "osi_sensorview_generated.h"

namespace {

std::vector<uint8_t> buildSensorViewMessage(const common::DataValue& value) {
    flatbuffers::FlatBufferBuilder builder(1024);

    if (!value.isObject()) {
        throw std::runtime_error("FlatBuffers serialize expects object");
    }

    const auto& obj = value.asObject();

    // Build Timestamp
    flatbuffers::Offset<osi::Timestamp> timestamp_offset = 0;
    if (obj.count("timestamp")) {
        const auto& ts = obj.at("timestamp").asObject();
        int64_t seconds = ts.count("seconds") ? ts.at("seconds").asInt() : 0;
        int32_t nanos = ts.count("nanos") ? static_cast<int32_t>(ts.at("nanos").asInt()) : 0;
        timestamp_offset = osi::CreateTimestamp(builder, seconds, nanos);
    }

    // Build sensor_name
    flatbuffers::Offset<flatbuffers::String> sensor_name_offset = 0;
    if (obj.count("sensor_name")) {
        sensor_name_offset = builder.CreateString(obj.at("sensor_name").asString());
    }

    // Build moving_object array
    std::vector<flatbuffers::Offset<osi::MovingObject>> moving_objects;
    if (obj.count("moving_object")) {
        for (const auto& mo : obj.at("moving_object").asArray()) {
            const auto& moObj = mo.asObject();

            uint64_t id = moObj.count("id") ? static_cast<uint64_t>(moObj.at("id").asInt()) : 0;

            // Build BaseMoving
            flatbuffers::Offset<osi::BaseMoving> base_offset = 0;
            if (moObj.count("base")) {
                const auto& baseObj = moObj.at("base").asObject();

                osi::Vector3d position(0, 0, 0);
                osi::Vector3d velocity(0, 0, 0);
                osi::Vector3d orientation(0, 0, 0);

                if (baseObj.count("position")) {
                    const auto& p = baseObj.at("position").asObject();
                    position = osi::Vector3d(
                        p.count("x") ? p.at("x").asDouble() : 0,
                        p.count("y") ? p.at("y").asDouble() : 0,
                        p.count("z") ? p.at("z").asDouble() : 0
                    );
                }
                if (baseObj.count("velocity")) {
                    const auto& v = baseObj.at("velocity").asObject();
                    velocity = osi::Vector3d(
                        v.count("x") ? v.at("x").asDouble() : 0,
                        v.count("y") ? v.at("y").asDouble() : 0,
                        v.count("z") ? v.at("z").asDouble() : 0
                    );
                }
                if (baseObj.count("orientation")) {
                    const auto& o = baseObj.at("orientation").asObject();
                    orientation = osi::Vector3d(
                        o.count("x") ? o.at("x").asDouble() : 0,
                        o.count("y") ? o.at("y").asDouble() : 0,
                        o.count("z") ? o.at("z").asDouble() : 0
                    );
                }

                base_offset = osi::CreateBaseMoving(builder, &position, &velocity, &orientation);
            }

            osi::ObjectType type = osi::ObjectType_TYPE_UNKNOWN;
            if (moObj.count("type")) {
                const auto& typeVal = moObj.at("type");
                if (typeVal.isString()) {
                    const auto& typeStr = typeVal.asString();
                    if (typeStr == "TYPE_VEHICLE") type = osi::ObjectType_TYPE_VEHICLE;
                    else if (typeStr == "TYPE_PEDESTRIAN") type = osi::ObjectType_TYPE_PEDESTRIAN;
                    else if (typeStr == "TYPE_ANIMAL") type = osi::ObjectType_TYPE_ANIMAL;
                } else if (typeVal.isInt()) {
                    type = static_cast<osi::ObjectType>(typeVal.asInt());
                }
            }

            moving_objects.push_back(osi::CreateMovingObject(builder, id, base_offset, type));
        }
    }

    auto moving_object_vector = builder.CreateVector(moving_objects);

    uint64_t sensor_id = obj.count("sensor_id") ? static_cast<uint64_t>(obj.at("sensor_id").asInt()) : 0;
    bool is_valid = obj.count("is_valid") ? obj.at("is_valid").asBool() : false;

    auto msg = osi::CreateSensorViewMessage(
        builder,
        timestamp_offset,
        sensor_id,
        moving_object_vector,
        sensor_name_offset,
        is_valid
    );

    builder.Finish(msg);

    const uint8_t* buf = builder.GetBufferPointer();
    size_t size = builder.GetSize();
    return std::vector<uint8_t>(buf, buf + size);
}

common::DataValue parseSensorViewMessage(const std::vector<uint8_t>& data) {
    auto msg = osi::GetSensorViewMessage(data.data());

    common::DataObject result;

    // Timestamp
    if (msg->timestamp()) {
        common::DataObject ts;
        ts["seconds"] = common::DataValue(msg->timestamp()->seconds());
        ts["nanos"] = common::DataValue(static_cast<int64_t>(msg->timestamp()->nanos()));
        result["timestamp"] = common::DataValue(std::move(ts));
    }

    result["sensor_id"] = common::DataValue(static_cast<int64_t>(msg->sensor_id()));

    if (msg->sensor_name()) {
        result["sensor_name"] = common::DataValue(msg->sensor_name()->str());
    }

    result["is_valid"] = common::DataValue(msg->is_valid());

    // Moving objects
    common::DataArray movingObjects;
    if (msg->moving_object()) {
        for (const auto* mo : *msg->moving_object()) {
            common::DataObject moObj;
            moObj["id"] = common::DataValue(static_cast<int64_t>(mo->id()));

            if (mo->base()) {
                common::DataObject base;
                if (mo->base()->position()) {
                    common::DataObject pos;
                    pos["x"] = common::DataValue(mo->base()->position()->x());
                    pos["y"] = common::DataValue(mo->base()->position()->y());
                    pos["z"] = common::DataValue(mo->base()->position()->z());
                    base["position"] = common::DataValue(std::move(pos));
                }
                if (mo->base()->velocity()) {
                    common::DataObject vel;
                    vel["x"] = common::DataValue(mo->base()->velocity()->x());
                    vel["y"] = common::DataValue(mo->base()->velocity()->y());
                    vel["z"] = common::DataValue(mo->base()->velocity()->z());
                    base["velocity"] = common::DataValue(std::move(vel));
                }
                if (mo->base()->orientation()) {
                    common::DataObject ori;
                    ori["x"] = common::DataValue(mo->base()->orientation()->x());
                    ori["y"] = common::DataValue(mo->base()->orientation()->y());
                    ori["z"] = common::DataValue(mo->base()->orientation()->z());
                    base["orientation"] = common::DataValue(std::move(ori));
                }
                moObj["base"] = common::DataValue(std::move(base));
            }

            switch (mo->type()) {
                case osi::ObjectType_TYPE_VEHICLE:
                    moObj["type"] = common::DataValue("TYPE_VEHICLE");
                    break;
                case osi::ObjectType_TYPE_PEDESTRIAN:
                    moObj["type"] = common::DataValue("TYPE_PEDESTRIAN");
                    break;
                case osi::ObjectType_TYPE_ANIMAL:
                    moObj["type"] = common::DataValue("TYPE_ANIMAL");
                    break;
                default:
                    moObj["type"] = common::DataValue("TYPE_UNKNOWN");
            }

            movingObjects.push_back(common::DataValue(std::move(moObj)));
        }
    }
    result["moving_object"] = common::DataValue(std::move(movingObjects));

    return common::DataValue(std::move(result));
}

// Register at static initialization
REGISTER_FLATBUFFERS_MESSAGE(osi::SensorViewMessage, buildSensorViewMessage, parseSensorViewMessage)

} // anonymous namespace

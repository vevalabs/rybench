#include <iox2/iceoryx2.hpp>

#include "osi_sensorview_generated.h"
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <iostream>
#include <atomic>
#include <csignal>
#include <string>

using namespace iox2;

std::atomic<bool> running{true};

void signalHandler(int) {
    running = false;
}

template<typename VecType>
void writeVector3d(rapidjson::PrettyWriter<rapidjson::StringBuffer>& writer, const VecType* vec) {
    if (!vec) {
        writer.Null();
        return;
    }
    writer.StartObject();
    writer.Key("x"); writer.Double(vec->x());
    writer.Key("y"); writer.Double(vec->y());
    writer.Key("z"); writer.Double(vec->z());
    writer.EndObject();
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    set_log_level_from_env_or(LogLevel::Info);

    // Create node
    auto node = NodeBuilder{}.create<ServiceType::Ipc>().value();

    // Create service
    auto service = node.service_builder(ServiceName::create("Benchmark/Default/Data").value())
                       .publish_subscribe<bb::Slice<uint8_t>>()
                       .open_or_create()
                       .value();

    // Create subscriber
    auto subscriber = service.subscriber_builder().create().value();

    std::cout << "FlatBuffers Consumer started. Waiting for messages... (Ctrl+C to stop)" << std::endl;

    uint64_t messageCount = 0;
    constexpr auto CYCLE_TIME = bb::Duration::from_millis(10);

    while (running && node.wait(CYCLE_TIME).has_value()) {
        auto sample = subscriber.receive().value();
        while (sample.has_value()) {
            messageCount++;
            auto payload = sample->payload();

            auto msg = osi::GetSensorViewMessage(payload.data());

            rapidjson::StringBuffer buffer;
            rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);

            writer.StartObject();

            if (msg->timestamp()) {
                writer.Key("timestamp");
                writer.StartObject();
                writer.Key("seconds"); writer.Int64(msg->timestamp()->seconds());
                writer.Key("nanos"); writer.Int(msg->timestamp()->nanos());
                writer.EndObject();
            }

            writer.Key("sensor_id"); writer.Uint(msg->sensor_id());
            writer.Key("sensor_name"); writer.String(msg->sensor_name() ? msg->sensor_name()->c_str() : "");
            writer.Key("is_valid"); writer.Bool(msg->is_valid());

            writer.Key("moving_object");
            writer.StartArray();
            if (msg->moving_object()) {
                for (const auto* mo : *msg->moving_object()) {
                    writer.StartObject();
                    writer.Key("id"); writer.Uint(mo->id());

                    if (mo->base()) {
                        writer.Key("base");
                        writer.StartObject();
                        writer.Key("position"); writeVector3d(writer, mo->base()->position());
                        writer.Key("velocity"); writeVector3d(writer, mo->base()->velocity());
                        writer.Key("orientation"); writeVector3d(writer, mo->base()->orientation());
                        writer.EndObject();
                    }

                    writer.Key("type");
                    switch (mo->type()) {
                        case osi::ObjectType_TYPE_VEHICLE:
                            writer.String("TYPE_VEHICLE");
                            break;
                        case osi::ObjectType_TYPE_PEDESTRIAN:
                            writer.String("TYPE_PEDESTRIAN");
                            break;
                        case osi::ObjectType_TYPE_ANIMAL:
                            writer.String("TYPE_ANIMAL");
                            break;
                        default:
                            writer.String("TYPE_UNKNOWN");
                    }

                    writer.EndObject();
                }
            }
            writer.EndArray();

            writer.EndObject();

            std::cout << "[" << messageCount << "] " << buffer.GetString() << std::endl;

            sample = subscriber.receive().value();
        }
    }

    std::cout << "\nTotal messages received: " << messageCount << std::endl;
    return 0;
}

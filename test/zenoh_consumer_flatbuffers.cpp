#include <zenoh.hxx>
#include "osi_sensorview_generated.h"
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <iostream>
#include <atomic>
#include <csignal>
#include <string>
#include <thread>
#include <chrono>

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

    try {
        // Open Zenoh session
        zenoh::Config config = zenoh::Config::create_default();
        auto session = zenoh::Session::open(std::move(config));

        // Subscribe to key expression: service/instance/event
        std::string keyExpr = "Benchmark/Default/Data";

        uint64_t messageCount = 0;

        auto callback = [&messageCount](zenoh::Sample& sample) {
            messageCount++;
            const auto& payload = sample.get_payload();
            auto data = payload.as_vector();

            auto msg = osi::GetSensorViewMessage(data.data());

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
        };

        auto on_drop = []() {};

        auto subscriber = session.declare_subscriber(zenoh::KeyExpr(keyExpr), callback, on_drop);

        std::cout << "Zenoh FlatBuffers Consumer started. Subscribing to: " << keyExpr << std::endl;
        std::cout << "Waiting for messages... (Ctrl+C to stop)" << std::endl;

        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\nTotal messages received: " << messageCount << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

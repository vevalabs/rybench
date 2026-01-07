#include <zenoh.hxx>
#include "osi_sensorview_generated.h"
#include <nlohmann/json.hpp>

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

nlohmann::json vector3dToJson(const osi::Vector3d* vec) {
    if (!vec) return nullptr;
    return {{"x", vec->x()}, {"y", vec->y()}, {"z", vec->z()}};
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

            nlohmann::json output;

            if (msg->timestamp()) {
                output["timestamp"] = {
                    {"seconds", msg->timestamp()->seconds()},
                    {"nanos", msg->timestamp()->nanos()}
                };
            }

            output["sensor_id"] = msg->sensor_id();
            output["sensor_name"] = msg->sensor_name() ? msg->sensor_name()->str() : "";
            output["is_valid"] = msg->is_valid();

            nlohmann::json movingObjects = nlohmann::json::array();
            if (msg->moving_object()) {
                for (const auto* mo : *msg->moving_object()) {
                    nlohmann::json moJson;
                    moJson["id"] = mo->id();

                    if (mo->base()) {
                        moJson["base"] = {
                            {"position", vector3dToJson(mo->base()->position())},
                            {"velocity", vector3dToJson(mo->base()->velocity())},
                            {"orientation", vector3dToJson(mo->base()->orientation())}
                        };
                    }

                    switch (mo->type()) {
                        case osi::ObjectType_TYPE_VEHICLE:
                            moJson["type"] = "TYPE_VEHICLE";
                            break;
                        case osi::ObjectType_TYPE_PEDESTRIAN:
                            moJson["type"] = "TYPE_PEDESTRIAN";
                            break;
                        case osi::ObjectType_TYPE_ANIMAL:
                            moJson["type"] = "TYPE_ANIMAL";
                            break;
                        default:
                            moJson["type"] = "TYPE_UNKNOWN";
                    }

                    movingObjects.push_back(moJson);
                }
            }
            output["moving_object"] = movingObjects;

            std::cout << "[" << messageCount << "] " << output.dump(2) << std::endl;
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

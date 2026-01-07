#include "signal_handler.hpp"
#include "iceoryx_publisher.hpp"
#include "iceoryx_posh/popo/untyped_subscriber.hpp"
#include "iceoryx_posh/mepoo/chunk_header.hpp"

#include "osi_sensorview_generated.h"
#include <nlohmann/json.hpp>

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

std::atomic<bool> running{true};

nlohmann::json vector3dToJson(const osi::Vector3d* vec) {
    if (!vec) return nullptr;
    return {{"x", vec->x()}, {"y", vec->y()}, {"z", vec->z()}};
}

int main() {
    publisher::IceoryxPublisher::initRuntime("benchmark_consumer_flatbuffers");
    common::SignalHandler::setup(running);

    iox::capro::ServiceDescription serviceDesc{
        iox::capro::IdString_t(iox::TruncateToCapacity, "Benchmark"),
        iox::capro::IdString_t(iox::TruncateToCapacity, "Default"),
        iox::capro::IdString_t(iox::TruncateToCapacity, "Data")
    };

    iox::popo::UntypedSubscriber subscriber(serviceDesc);

    std::cout << "FlatBuffers Consumer started. Waiting for messages... (Ctrl+C to stop)" << std::endl;

    uint64_t messageCount = 0;

    while (running) {
        subscriber.take()
            .and_then([&](const void* payload) {
                messageCount++;
                auto header = iox::mepoo::ChunkHeader::fromUserPayload(payload);

                auto msg = osi::GetSensorViewMessage(payload);

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

                subscriber.release(payload);
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "\nTotal messages received: " << messageCount << std::endl;
    return 0;
}

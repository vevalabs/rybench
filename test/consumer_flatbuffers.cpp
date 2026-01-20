#include "signal_handler.hpp"
#include "iceoryx_publisher.hpp"
#include "iceoryx_posh/popo/untyped_subscriber.hpp"
#include "iceoryx_posh/mepoo/chunk_header.hpp"

#include "osi_sensorview_generated.h"
#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

std::atomic<bool> running{true};

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
    rybench::publisher::IceoryxPublisher::initRuntime("benchmark_consumer_flatbuffers");
    rybench::common::SignalHandler::setup(running);

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

                subscriber.release(payload);
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "\nTotal messages received: " << messageCount << std::endl;
    return 0;
}

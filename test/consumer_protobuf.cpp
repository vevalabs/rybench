#include "signal_handler.hpp"
#include "iceoryx_publisher.hpp"
#include "iceoryx_posh/popo/untyped_subscriber.hpp"
#include "iceoryx_posh/mepoo/chunk_header.hpp"

#include "osi_sensorview.pb.h"
#include <google/protobuf/util/json_util.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

std::atomic<bool> running{true};

int main() {
    rybench::publisher::IceoryxPublisher::initRuntime("benchmark_consumer_protobuf");
    rybench::common::SignalHandler::setup(running);

    iox::capro::ServiceDescription serviceDesc{
        iox::capro::IdString_t(iox::TruncateToCapacity, "Benchmark"),
        iox::capro::IdString_t(iox::TruncateToCapacity, "Default"),
        iox::capro::IdString_t(iox::TruncateToCapacity, "Data")
    };

    iox::popo::UntypedSubscriber subscriber(serviceDesc);

    std::cout << "Protobuf Consumer started. Waiting for messages... (Ctrl+C to stop)" << std::endl;

    uint64_t messageCount = 0;

    while (running) {
        subscriber.take()
            .and_then([&](const void* payload) {
                messageCount++;
                auto header = iox::mepoo::ChunkHeader::fromUserPayload(payload);

                osi::SensorViewMessage msg;
                if (msg.ParseFromArray(payload, header->userPayloadSize())) {
                    std::string jsonOutput;
                    google::protobuf::util::JsonPrintOptions options;
                    options.add_whitespace = true;
                    google::protobuf::util::MessageToJsonString(msg, &jsonOutput, options);
                    std::cout << "[" << messageCount << "] " << jsonOutput << std::endl;
                } else {
                    std::cerr << "[" << messageCount << "] Failed to parse protobuf message" << std::endl;
                }

                subscriber.release(payload);
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "\nTotal messages received: " << messageCount << std::endl;
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}

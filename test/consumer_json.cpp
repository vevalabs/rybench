#include "common/signal_handler.hpp"
#include "publisher/iceoryx_publisher.hpp"
#include "iceoryx_posh/popo/untyped_subscriber.hpp"
#include "iceoryx_posh/mepoo/chunk_header.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>
#include <string>

std::atomic<bool> running{true};

int main() {
    publisher::IceoryxPublisher::initRuntime("benchmark_consumer");
    common::SignalHandler::setup(running);

    iox::capro::ServiceDescription serviceDesc{
        iox::capro::IdString_t(iox::TruncateToCapacity, "Benchmark"),
        iox::capro::IdString_t(iox::TruncateToCapacity, "Default"),
        iox::capro::IdString_t(iox::TruncateToCapacity, "Data")
    };

    iox::popo::UntypedSubscriber subscriber(serviceDesc);

    std::cout << "Consumer started. Waiting for messages... (Ctrl+C to stop)" << std::endl;

    uint64_t messageCount = 0;

    while (running) {
        subscriber.take()
            .and_then([&](const void* payload) {
                messageCount++;
                auto header = iox::mepoo::ChunkHeader::fromUserPayload(payload);
                std::string msg(static_cast<const char*>(payload), header->userPayloadSize());
                std::cout << "[" << messageCount << "] " << msg << std::endl;
                subscriber.release(payload);
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    std::cout << "\nTotal messages received: " << messageCount << std::endl;
    return 0;
}

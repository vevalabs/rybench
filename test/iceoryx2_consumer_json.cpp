#include <iox2/iceoryx2.hpp>

#include <iostream>
#include <atomic>
#include <csignal>
#include <string>

using namespace iox2;

std::atomic<bool> running{true};

void signalHandler(int) {
    running = false;
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

    std::cout << "Consumer started. Waiting for messages... (Ctrl+C to stop)" << std::endl;

    uint64_t messageCount = 0;
    constexpr auto CYCLE_TIME = bb::Duration::from_millis(10);

    while (running && node.wait(CYCLE_TIME).has_value()) {
        auto sample = subscriber.receive().value();
        while (sample.has_value()) {
            messageCount++;
            auto payload = sample->payload();
            std::string msg(reinterpret_cast<const char*>(payload.data()),
                           payload.number_of_elements());
            std::cout << "[" << messageCount << "] " << msg << std::endl;
            sample = subscriber.receive().value();
        }
    }

    std::cout << "\nTotal messages received: " << messageCount << std::endl;
    return 0;
}

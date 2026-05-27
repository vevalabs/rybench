#include "iceoryx2_consumer.hpp"

#include <iox2/iceoryx2.hpp>

#include <iostream>

namespace rybench::transport::iceoryx2 {


struct Iceoryx2Consumer::Impl {
    iox2::Node<iox2::ServiceType::Ipc> node;
    iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<uint8_t>, void> subscriber;

    Impl(iox2::Node<iox2::ServiceType::Ipc>&& n,
         iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<uint8_t>, void>&& s)
        : node(std::move(n)), subscriber(std::move(s)) {}
};

Iceoryx2Consumer::Iceoryx2Consumer() = default;

Iceoryx2Consumer::~Iceoryx2Consumer() {
    shutdown();
}

std::expected<void, std::string> Iceoryx2Consumer::init(const core::Config& config) {
    service_ = config.get("iceoryx2", "service", "Benchmark");
    instance_ = config.get("iceoryx2", "instance", "Default");
    event_ = config.get("iceoryx2", "event", "Data");
    outputPath_ = config.get("iceoryx2", "output", "dump/iceoryx2-received.bin");

    const std::string serviceName = service_ + "/" + instance_ + "/" + event_;

    outputFile_.open(outputPath_, std::ios::binary | std::ios::trunc);
    if (!outputFile_.is_open())
        return std::unexpected("[Iceoryx2] Failed to open output file: " + outputPath_);

    auto node_result = iox2::NodeBuilder{}.create<iox2::ServiceType::Ipc>();
    if (!node_result) return std::unexpected("[Iceoryx2] Failed to create node");

    auto node = std::move(node_result.value());

    auto service_name = iox2::ServiceName::create(serviceName.c_str());
    if (!service_name) return std::unexpected("[Iceoryx2] Invalid service name: " + serviceName);

    auto service_result =
        node.service_builder(service_name.value())
            .publish_subscribe<iox2::bb::Slice<uint8_t>>()
            .open_or_create();

    if (!service_result) return std::unexpected("[Iceoryx2] Failed to open/create service: " + serviceName);

    auto service = std::move(service_result.value());

    auto sub_result = service.subscriber_builder().create();
    if (!sub_result) return std::unexpected("[Iceoryx2] Failed to create subscriber");

    impl_ = std::make_unique<Impl>(std::move(node), std::move(sub_result.value()));
    initialized_ = true;

    std::cout << "[Iceoryx2] Consumer initialized: " << serviceName << "\n";
    std::cout << "[Iceoryx2] Writing to: " << outputPath_ << "\n";
    return {};
}

void Iceoryx2Consumer::run(std::atomic<bool>& running) {
    if (!initialized_ || !impl_) {
        return;
    }

    constexpr auto CYCLE_TIME = iox2::bb::Duration::from_millis(10);

    while (running && impl_->node.wait(CYCLE_TIME).has_value()) {
        auto sample = impl_->subscriber.receive().value();
        while (sample.has_value()) {
            auto payload = sample->payload();

            // Write to file: size + data
            uint32_t msgSize = static_cast<uint32_t>(payload.number_of_elements());
            outputFile_.write(reinterpret_cast<const char*>(&msgSize), sizeof(msgSize));
            outputFile_.write(reinterpret_cast<const char*>(payload.data()), msgSize);

            messageCount_++;
            if (messageCount_ % 100 == 0) {
                std::cout << "Received " << messageCount_ << " messages" << std::endl;
            }

            sample = impl_->subscriber.receive().value();
        }
    }
}

void Iceoryx2Consumer::shutdown() {
    if (initialized_) {
        impl_.reset();
        if (outputFile_.is_open()) {
            outputFile_.close();
        }
        std::cout << "[Iceoryx2] Consumer shutdown. Received " << messageCount_ << " messages.\n";
        std::cout << "[Iceoryx2] Saved to: " << outputPath_ << "\n";
        initialized_ = false;
    }
}

} // namespace rybench::transport::iceoryx2

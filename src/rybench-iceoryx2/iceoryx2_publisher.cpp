#include "iceoryx2_publisher.hpp"

#include <iox2/iceoryx2.hpp>

#include <iostream>
#include <cstring>

namespace rybench {

using namespace iox2;

struct Iceoryx2Publisher::Impl {
    Node<ServiceType::Ipc> node;
    Publisher<ServiceType::Ipc, bb::Slice<uint8_t>, void> publisher;

    Impl(Node<ServiceType::Ipc>&& n,
         Publisher<ServiceType::Ipc, bb::Slice<uint8_t>, void>&& p)
        : node(std::move(n)), publisher(std::move(p)) {}
};

Iceoryx2Publisher::Iceoryx2Publisher() = default;

Iceoryx2Publisher::~Iceoryx2Publisher() {
    shutdown();
}

void Iceoryx2Publisher::init(const common::Config& config) {
    // Read config from [iceoryx2] section
    service_ = config.get("iceoryx2", "service", "Benchmark");
    instance_ = config.get("iceoryx2", "instance", "Default");
    event_ = config.get("iceoryx2", "event", "Data");
    maxPayloadSize_ = static_cast<std::size_t>(config.getInt("iceoryx2", "max_payload_size", 4096));
    rateHz_ = config.getInt("iceoryx2", "rate_hz", 10);

    const std::string serviceName = service_ + "/" + instance_ + "/" + event_;

    // Create node
    auto node_result = NodeBuilder{}.create<ServiceType::Ipc>();
    if (!node_result) {
        std::cerr << "[Iceoryx2] Failed to create node\n";
        return;
    }

    auto node = std::move(node_result.value());

    // Create service
    auto service_name = ServiceName::create(serviceName.c_str());
    if (!service_name) {
        std::cerr << "[Iceoryx2] Invalid service name\n";
        return;
    }

    auto service_result =
        node.service_builder(service_name.value())
            .publish_subscribe<bb::Slice<uint8_t>>()
            .open_or_create();

    if (!service_result) {
        std::cerr << "[Iceoryx2] Failed to open/create service\n";
        return;
    }

    auto service = std::move(service_result.value());

    // Create publisher
    auto pub_result =
        service.publisher_builder()
            .initial_max_slice_len(maxPayloadSize_)
            .create();

    if (!pub_result) {
        std::cerr << "[Iceoryx2] Failed to create publisher\n";
        return;
    }

    impl_ = std::make_unique<Impl>(
        std::move(node),
        std::move(pub_result.value())
    );

    initialized_ = true;

    std::cout << "[Iceoryx2] Publisher initialized: "
              << serviceName
              << " (max payload "
              << maxPayloadSize_
              << " bytes)\n";
}

bool Iceoryx2Publisher::publish(const void* data, std::size_t size) {
    if (!initialized_ || !impl_) {
        return false;
    }

    if (size > maxPayloadSize_) {
        std::cerr << "[Iceoryx2] Payload too large\n";
        return false;
    }

    auto loan_result = impl_->publisher.loan_slice_uninit(size);
    if (!loan_result) {
        std::cerr << "[Iceoryx2] Loan failed\n";
        return false;
    }

    auto sample = std::move(loan_result.value());
    const uint8_t* src = static_cast<const uint8_t*>(data);

    auto initialized_sample =
        sample.write_from_fn([&](std::size_t i) { return src[i]; });

    auto send_result = send(std::move(initialized_sample));
    if (!send_result) {
        std::cerr << "[Iceoryx2] Send failed\n";
        return false;
    }

    return true;
}

void Iceoryx2Publisher::shutdown() {
    if (initialized_) {
        impl_.reset();
        initialized_ = false;
        std::cout << "[Iceoryx2] Publisher shutdown\n";
    }
}

} // namespace rybench

#include "iceoryx2_publisher.hpp"

#include <iox2/iceoryx2.hpp>

#include <iostream>
#include <cstring>

namespace rybench::transport::iceoryx2 {

struct Iceoryx2Publisher::Impl {
    iox2::Node<iox2::ServiceType::Ipc> node;
    iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<uint8_t>, void> publisher;

    Impl(iox2::Node<iox2::ServiceType::Ipc>&& n,
         iox2::Publisher<iox2::ServiceType::Ipc, iox2::bb::Slice<uint8_t>, void>&& p)
        : node(std::move(n)), publisher(std::move(p)) {}
};

Iceoryx2Publisher::Iceoryx2Publisher() = default;

Iceoryx2Publisher::~Iceoryx2Publisher() {
    shutdown();
}

std::expected<void, std::string> Iceoryx2Publisher::init(const common::Config& config) {
    service_ = config.get("iceoryx2", "service", "Benchmark");
    instance_ = config.get("iceoryx2", "instance", "Default");
    event_ = config.get("iceoryx2", "event", "Data");
    maxPayloadSize_ = static_cast<std::size_t>(config.getInt("iceoryx2", "max_payload_size", 4096));
    rateHz_ = config.getInt("iceoryx2", "rate_hz", 10);

    const std::string serviceName = service_ + "/" + instance_ + "/" + event_;

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

    auto pub_result =
        service.publisher_builder()
            .initial_max_slice_len(maxPayloadSize_)
            .create();

    if (!pub_result) return std::unexpected("[Iceoryx2] Failed to create publisher");

    impl_ = std::make_unique<Impl>(std::move(node), std::move(pub_result.value()));
    initialized_ = true;

    std::cout << "[Iceoryx2] Publisher initialized: " << serviceName
              << " (max payload " << maxPayloadSize_ << " bytes)\n";
    return {};
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

} // namespace rybench::transport::iceoryx2

#include "iceoryx2_publisher.hpp"

#include <iox2/iceoryx2.hpp>

#include <iostream>
#include <cstring>
#include <chrono>
#include <thread>

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

std::expected<void, std::string> Iceoryx2Publisher::init(const core::Config& config) {
    service_ = config.get("iceoryx2", "service", "Benchmark");
    instance_ = config.get("iceoryx2", "instance", "Default");
    event_ = config.get("iceoryx2", "event", "Data");
    maxPayloadSize_ = static_cast<std::size_t>(config.getInt("iceoryx2", "max_payload_size", 4096));
    rateHz_ = config.getInt("iceoryx2", "rate_hz", 10);
    stampTime_ = (config.get("iceoryx2", "stamp_time", "false") == "true");
    // Delivery policy: default DiscardSample (real-time, drop under overload). lossless
    // = Block → publisher waits for the subscriber (backpressure, 0 drop, not real-time).
    const bool lossless = (config.get("iceoryx2", "lossless", "false") == "true");

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
            .unable_to_deliver_strategy(lossless ? iox2::UnableToDeliverStrategy::Block
                                                 : iox2::UnableToDeliverStrategy::DiscardSample)
            .create();

    if (!pub_result) return std::unexpected("[Iceoryx2] Failed to create publisher");

    auto publisher = std::move(pub_result.value());

    // Lossless: wait until at least one subscriber is connected before publishing, so
    // the startup firehose isn't sent into the void (Block only backpressures for
    // already-connected subscribers). update_connections() refreshes the publisher's
    // view; number_of_subscribers() reports the service's connected subscribers. Bounded
    // to ~5s so a solo publisher still starts.
    if (lossless) {
        for (int i = 0; i < 5000 && service.dynamic_config().number_of_subscribers() == 0; ++i) {
            (void)publisher.update_connections();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        std::cout << "[Iceoryx2] Subscribers connected: "
                  << service.dynamic_config().number_of_subscribers() << "\n";
    }

    impl_ = std::make_unique<Impl>(std::move(node), std::move(publisher));
    initialized_ = true;

    std::cout << "[Iceoryx2] Publisher initialized: " << serviceName
              << " (max payload " << maxPayloadSize_ << " bytes, "
              << (lossless ? "lossless/Block" : "drop/DiscardSample") << ")\n";
    return {};
}

bool Iceoryx2Publisher::publish(const void* data, std::size_t size) {
    if (!initialized_ || !impl_) {
        return false;
    }

    // Latency mode: prepend an 8-byte steady_clock ns send-timestamp.
    const std::size_t hdr = stampTime_ ? sizeof(uint64_t) : 0;
    const std::size_t total = size + hdr;
    if (total > maxPayloadSize_) {
        std::cerr << "[Iceoryx2] Payload too large\n";
        return false;
    }

    auto loan_result = impl_->publisher.loan_slice_uninit(total);
    if (!loan_result) {
        std::cerr << "[Iceoryx2] Loan failed\n";
        return false;
    }

    auto sample = std::move(loan_result.value());

    // Bulk memcpy into the loaned shared-memory slice instead of a per-byte
    // write_from_fn callback (which is ~1 µs/msg of pure copy overhead).
    auto dest = sample.payload_mut();
    if (hdr) {
        uint64_t ns = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        std::memcpy(dest.data(), &ns, hdr);
    }
    std::memcpy(dest.data() + hdr, data, size);

    auto send_result = send(assume_init(std::move(sample)));
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

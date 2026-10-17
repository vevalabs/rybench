#include "zenoh_publisher.hpp"

#include <zenoh.hxx>
#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>

namespace rybench::transport::zenoh {

struct ZenohPublisher::Impl {
    ::zenoh::Session session;
    ::zenoh::Publisher publisher;

    Impl(::zenoh::Session&& s, ::zenoh::Publisher&& p)
        : session(std::move(s)), publisher(std::move(p)) {}
};

ZenohPublisher::ZenohPublisher() = default;

ZenohPublisher::~ZenohPublisher() {
    shutdown();
}

std::expected<void, std::string> ZenohPublisher::init(const core::Config& config) {
    keyExpr_ = config.get("zenoh", "key_expr", "Benchmark/Default/Data");
    maxPayloadSize_ = static_cast<size_t>(config.getInt("zenoh", "max_payload_size", 4096));
    rateHz_ = config.getInt("zenoh", "rate_hz", 10);
    stampTime_ = (config.get("zenoh", "stamp_time", "false") == "true");

    try {
        ::zenoh::Config zconfig = ::zenoh::Config::create_default();
        auto session = ::zenoh::Session::open(std::move(zconfig));
        auto publisher = session.declare_publisher(::zenoh::KeyExpr(keyExpr_));

        impl_ = std::make_unique<Impl>(std::move(session), std::move(publisher));
        initialized_ = true;

        std::cout << "[Zenoh] Publisher initialized: " << keyExpr_
                  << " (max payload: " << maxPayloadSize_ << " bytes)\n";
        return {};
    } catch (const std::exception& e) {
        return std::unexpected(std::string("[Zenoh] Failed to initialize: ") + e.what());
    }
}

bool ZenohPublisher::publish(const void* data, size_t size) {
    if (!initialized_ || !impl_) {
        return false;
    }

    if (size > maxPayloadSize_) {
        std::cerr << "[Zenoh] Payload size " << size
                  << " exceeds max " << maxPayloadSize_ << std::endl;
        return false;
    }

    try {
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        std::vector<uint8_t> buf;
        if (stampTime_) {
            // [8-byte send ns][message] for latency measurement.
            uint64_t ns = static_cast<uint64_t>(
                std::chrono::steady_clock::now().time_since_epoch().count());
            buf.resize(sizeof(ns) + size);
            std::memcpy(buf.data(), &ns, sizeof(ns));
            std::memcpy(buf.data() + sizeof(ns), ptr, size);
        } else {
            buf.assign(ptr, ptr + size);
        }
        impl_->publisher.put(::zenoh::Bytes(std::move(buf)));
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[Zenoh] Failed to publish: " << e.what() << std::endl;
        return false;
    }
}

void ZenohPublisher::shutdown() {
    if (initialized_) {
        impl_.reset();
        initialized_ = false;
        std::cout << "[Zenoh] Publisher shutdown" << std::endl;
    }
}

} // namespace rybench::transport::zenoh

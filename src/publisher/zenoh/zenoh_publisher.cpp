#include "zenoh_publisher.hpp"
#include <iostream>
#include <vector>

namespace rybench {
namespace publisher {

ZenohPublisher::~ZenohPublisher() {
    shutdown();
}

void ZenohPublisher::init(const PublisherConfig& config) {
    config_ = config;

    // Build key expression: service/instance/event
    keyExpr_ = config_.serviceName + "/" + config_.instanceName + "/" + config_.eventName;

    try {
        // Open Zenoh session with default config
        zenoh::Config zconfig = zenoh::Config::create_default();
        session_ = std::make_unique<zenoh::Session>(zenoh::Session::open(std::move(zconfig)));

        // Create publisher
        publisher_ = std::make_unique<zenoh::Publisher>(
            session_->declare_publisher(zenoh::KeyExpr(keyExpr_))
        );

        initialized_ = true;

        std::cout << "[ZenohPublisher] Initialized: " << keyExpr_
                  << " (max payload: " << config_.maxPayloadSize << " bytes)" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "[ZenohPublisher] Failed to initialize: " << e.what() << std::endl;
    }
}

bool ZenohPublisher::publish(const void* data, size_t size) {
    if (!initialized_ || !publisher_) {
        return false;
    }

    if (size > config_.maxPayloadSize) {
        std::cerr << "[ZenohPublisher] Payload size " << size
                  << " exceeds max " << config_.maxPayloadSize << std::endl;
        return false;
    }

    try {
        // Create bytes from raw data (copy into vector for new API)
        const uint8_t* ptr = static_cast<const uint8_t*>(data);
        std::vector<uint8_t> buf(ptr, ptr + size);
        publisher_->put(zenoh::Bytes(std::move(buf)));
        return true;
    } catch (const std::exception& e) {
        std::cerr << "[ZenohPublisher] Failed to publish: " << e.what() << std::endl;
        return false;
    }
}

void ZenohPublisher::shutdown() {
    if (initialized_) {
        publisher_.reset();
        session_.reset();
        initialized_ = false;
        std::cout << "[ZenohPublisher] Shutdown" << std::endl;
    }
}

std::string ZenohPublisher::name() const {
    return "zenoh";
}

} // namespace publisher
} // namespace rybench

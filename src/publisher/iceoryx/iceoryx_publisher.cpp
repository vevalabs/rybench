#include "iceoryx_publisher.hpp"
#include <cstring>
#include <iostream>

namespace publisher {

void IceoryxPublisher::initRuntime(const std::string& appName) {
    iox::runtime::PoshRuntime::initRuntime(
        iox::RuntimeName_t(iox::TruncateToCapacity, appName.c_str()));
}

IceoryxPublisher::~IceoryxPublisher() {
    shutdown();
}

void IceoryxPublisher::init(const PublisherConfig& config) {
    config_ = config;

    iox::capro::ServiceDescription serviceDesc{
        iox::capro::IdString_t(iox::TruncateToCapacity, config_.serviceName.c_str()),
        iox::capro::IdString_t(iox::TruncateToCapacity, config_.instanceName.c_str()),
        iox::capro::IdString_t(iox::TruncateToCapacity, config_.eventName.c_str())
    };

    publisher_ = std::make_unique<iox::popo::UntypedPublisher>(serviceDesc);
    initialized_ = true;

    std::cout << "[IceoryxPublisher] Initialized: "
              << config_.serviceName << "/" << config_.instanceName << "/" << config_.eventName
              << " (max payload: " << config_.maxPayloadSize << " bytes)" << std::endl;
}

bool IceoryxPublisher::publish(const void* data, size_t size) {
    if (!initialized_ || !publisher_) {
        return false;
    }

    if (size > config_.maxPayloadSize) {
        std::cerr << "[IceoryxPublisher] Payload size " << size
                  << " exceeds max " << config_.maxPayloadSize << std::endl;
        return false;
    }

    bool success = true;
    publisher_->loan(size)
        .and_then([&](auto& userPayload) {
            std::memcpy(userPayload, data, size);
            publisher_->publish(userPayload);
        })
        .or_else([&success](auto& error) {
            std::cerr << "[IceoryxPublisher] Failed to loan memory: "
                      << static_cast<uint64_t>(error) << std::endl;
            success = false;
        });

    return success;
}

void IceoryxPublisher::shutdown() {
    if (publisher_) {
        publisher_.reset();
        initialized_ = false;
        std::cout << "[IceoryxPublisher] Shutdown" << std::endl;
    }
}

std::string IceoryxPublisher::name() const {
    return "iceoryx";
}

} // namespace publisher

#pragma once

#include "common/config.hpp"
#include <cstddef>
#include <memory>
#include <string>

namespace publisher {

struct PublisherConfig {
    std::string serviceName = "Benchmark";
    std::string instanceName = "Instance";
    std::string eventName = "Event";
    size_t maxPayloadSize = 4096;
};

class IPublisher {
public:
    virtual ~IPublisher() = default;

    virtual void init(const PublisherConfig& config) = 0;
    virtual bool publish(const void* data, size_t size) = 0;
    virtual void shutdown() = 0;
    virtual std::string name() const = 0;
};

using PublisherPtr = std::unique_ptr<IPublisher>;

} // namespace publisher

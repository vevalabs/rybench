#pragma once

#include "config.hpp"

#include <memory>
#include <string>
#include <cstddef>

namespace zenoh {
class Session;
class Publisher;
}

namespace rybench {

class ZenohPublisher {
public:
    ZenohPublisher();
    ~ZenohPublisher();

    void init(const common::Config& config);
    bool publish(const void* data, size_t size);
    void shutdown();

    int rateHz() const { return rateHz_; }

private:
    std::string keyExpr_;
    size_t maxPayloadSize_ = 4096;
    int rateHz_ = 10;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool initialized_ = false;
};

} // namespace rybench

#pragma once

#include <rybench/transport/publisher.hpp>

#include <memory>
#include <string>
#include <cstddef>

namespace zenoh {
class Session;
class Publisher;
}

namespace rybench::transport::zenoh {

class ZenohPublisher : public rybench::transport::Publisher {
public:
    ZenohPublisher();
    ~ZenohPublisher();

    std::expected<void, std::string> init(const common::Config& config) override;
    bool publish(const void* data, std::size_t size) override;
    void shutdown() override;
    int rateHz() const override { return rateHz_; }

private:
    std::string keyExpr_;
    std::size_t maxPayloadSize_ = 4096;
    int rateHz_ = 10;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    bool initialized_ = false;
};

} // namespace rybench::transport::zenoh

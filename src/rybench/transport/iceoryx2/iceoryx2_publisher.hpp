#pragma once

#include <rybench/transport/publisher.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>

namespace rybench::transport::iceoryx2 {

class Iceoryx2Publisher : public rybench::transport::Publisher {
public:
    Iceoryx2Publisher();
    ~Iceoryx2Publisher();

    std::expected<void, std::string> init(const core::Config& config) override;
    bool publish(const void* data, std::size_t size) override;
    void shutdown() override;
    int rateHz() const override { return rateHz_; }

private:
    std::string service_;
    std::string instance_;
    std::string event_;
    std::size_t maxPayloadSize_ = 4096;
    int rateHz_ = 10;
    bool initialized_{false};

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rybench::transport::iceoryx2

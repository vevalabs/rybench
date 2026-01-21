#pragma once

#include "config.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <memory>

namespace rybench {

class Iceoryx2Publisher {
public:
    Iceoryx2Publisher();
    ~Iceoryx2Publisher();

    void init(const common::Config& config);
    bool publish(const void* data, std::size_t size);
    void shutdown();

    int rateHz() const { return rateHz_; }

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

} // namespace rybench

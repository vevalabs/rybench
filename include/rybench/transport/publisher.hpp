#pragma once
#include <rybench/core/config.hpp>
#include <expected>
#include <string>
#include <cstddef>

namespace rybench::transport {

class Publisher {
public:
    virtual ~Publisher() = default;
    virtual std::expected<void, std::string> init(const core::Config& config) = 0;
    virtual bool publish(const void* data, std::size_t size) = 0;
    virtual void shutdown() = 0;
    virtual int rateHz() const = 0;
};

} // namespace rybench::transport

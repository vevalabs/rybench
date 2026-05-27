#pragma once
#include <rybench/core/config.hpp>
#include <expected>
#include <string>
#include <atomic>
#include <cstdint>

namespace rybench::transport {

class Consumer {
public:
    virtual ~Consumer() = default;
    virtual std::expected<void, std::string> init(const core::Config& config) = 0;
    virtual void run(std::atomic<bool>& running) = 0;
    virtual void shutdown() = 0;
    virtual std::uint64_t messageCount() const = 0;
};

} // namespace rybench::transport

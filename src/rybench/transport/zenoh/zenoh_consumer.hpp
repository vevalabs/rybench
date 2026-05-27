#pragma once

#include <rybench/transport/consumer.hpp>

#include <string>
#include <fstream>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <atomic>

namespace rybench::transport::zenoh {

class ZenohConsumer : public rybench::transport::Consumer {
public:
    ZenohConsumer();
    ~ZenohConsumer();

    std::expected<void, std::string> init(const core::Config& config) override;
    void run(std::atomic<bool>& running) override;
    void shutdown() override;
    uint64_t messageCount() const override { return messageCount_; }

private:
    std::string keyExpr_;
    std::string outputPath_;
    std::ofstream outputFile_;
    bool initialized_ = false;
    std::atomic<uint64_t> messageCount_{0};

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rybench::transport::zenoh

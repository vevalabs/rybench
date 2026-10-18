#pragma once

#include <rybench/transport/consumer.hpp>

#include <string>
#include <fstream>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <atomic>
#include <chrono>

namespace rybench::monitor { class IMonitorSink; }

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

    // Online monitor mode: feed each received message to a monitor backend (no file).
    std::unique_ptr<monitor::IMonitorSink> monitorSink_;
    std::chrono::steady_clock::time_point monStart_, monLast_;
    bool monStarted_ = false;
    bool monLatency_ = false;   // messages carry an 8-byte send ns prefix

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rybench::transport::zenoh

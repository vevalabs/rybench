#pragma once

#include <rybench/transport/consumer.hpp>
#include <rybench/serializer/serializer.hpp>

#include <string>
#include <fstream>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <atomic>
#include <chrono>

namespace rybench::monitor { class MonitorSink; }

namespace rybench::transport::iceoryx2 {

class Iceoryx2Consumer : public rybench::transport::Consumer {
public:
    Iceoryx2Consumer();
    ~Iceoryx2Consumer();

    std::expected<void, std::string> init(const core::Config& config) override;
    void run(std::atomic<bool>& running) override;
    void shutdown() override;
    uint64_t messageCount() const override { return messageCount_; }

private:
    std::string service_;
    std::string instance_;
    std::string event_;
    std::string outputPath_;
    std::ofstream outputFile_;
    bool initialized_ = false;
    bool writeJson_ = false;                 // deserialize received FlatBuffers to JSON
    serializer::SerializerPtr jsonSer_;      // used only when writeJson_ is set
    uint64_t messageCount_ = 0;

    // Online monitor mode: feed each received message to a Reelay monitor (no file).
    std::unique_ptr<monitor::MonitorSink> monitorSink_;
    std::chrono::steady_clock::time_point monStart_, monLast_;
    bool monStarted_ = false;
    bool monLatency_ = false;   // messages carry an 8-byte send ns prefix (publish->verdict)
    bool monProc_ = false;      // time reelay's own decode+monitor per event (receive->verdict)

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rybench::transport::iceoryx2

#pragma once

#include <rybench/transport/consumer.hpp>

#include <string>
#include <fstream>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <atomic>

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
    uint64_t messageCount_ = 0;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rybench::transport::iceoryx2

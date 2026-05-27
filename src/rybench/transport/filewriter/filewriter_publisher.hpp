#pragma once

#include <rybench/transport/publisher.hpp>

#include <cstdint>
#include <fstream>
#include <string>

namespace rybench::transport::filewriter {

class FilewriterPublisher : public Publisher {
public:
    std::expected<void, std::string> init(const core::Config& config) override;
    bool publish(const void* data, std::size_t size) override;
    void shutdown() override;
    int rateHz() const override { return rateHz_; }

private:
    std::string outputPath_;
    int rateHz_ = 10;
    std::ofstream outputFile_;
    uint64_t messageCount_ = 0;
};

} // namespace rybench::transport::filewriter

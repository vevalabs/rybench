#include "filewriter_publisher.hpp"

#include <iostream>

namespace rybench::transport::filewriter {

std::expected<void, std::string> FilewriterPublisher::init(const core::Config& config) {
    outputPath_ = config.get("filewriter", "output", "dump/benchmark.bin");
    rateHz_     = config.getInt("filewriter", "rate_hz", 10);

    outputFile_.open(outputPath_, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!outputFile_.is_open())
        return std::unexpected("[Filewriter] Failed to open output file: " + outputPath_);

    std::cout << "[Filewriter] Output: " << outputPath_ << "\n";
    return {};
}

bool FilewriterPublisher::publish(const void* data, std::size_t size) {
    uint32_t msgSize = static_cast<uint32_t>(size);
    outputFile_.write(reinterpret_cast<const char*>(&msgSize), sizeof(msgSize));
    outputFile_.write(reinterpret_cast<const char*>(data), size);
    ++messageCount_;
    return true;
}

void FilewriterPublisher::shutdown() {
    if (outputFile_.is_open()) {
        outputFile_.close();
        std::cout << "[Filewriter] Saved to: " << outputPath_ << "\n";
        std::cout << "[Filewriter] Total messages: " << messageCount_ << "\n";
    }
}

} // namespace rybench::transport::filewriter

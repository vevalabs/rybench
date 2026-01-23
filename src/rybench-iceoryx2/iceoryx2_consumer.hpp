#pragma once

#include "config.hpp"

#include <string>
#include <fstream>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <atomic>

namespace rybench {

class Iceoryx2Consumer {
public:
    Iceoryx2Consumer();
    ~Iceoryx2Consumer();

    void init(const common::Config& config);
    void run(std::atomic<bool>& running);
    void shutdown();

    uint64_t messageCount() const { return messageCount_; }

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

} // namespace rybench

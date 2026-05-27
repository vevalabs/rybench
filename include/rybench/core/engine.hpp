#pragma once
#include <rybench/core/config.hpp>
#include <rybench/transport/publisher.hpp>
#include <rybench/transport/consumer.hpp>
#include <atomic>

namespace rybench::core {

class Engine {
public:
    Engine(const Config& config, std::atomic<bool>& running);

    int run(transport::Publisher& publisher);
    int run(transport::Consumer& consumer);

private:
    const Config& config_;
    std::atomic<bool>& running_;
};

} // namespace rybench::core

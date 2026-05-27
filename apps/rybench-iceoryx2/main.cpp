#include <rybench/transport/iceoryx2/iceoryx2_runner.hpp>
#include <rybench/common/config.hpp>
#include <rybench/common/signal_handler.hpp>

#include <atomic>
#include <iostream>

std::atomic<bool> running{true};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: rybench-iceoryx2 <node-name>  (e.g. publisher@pub1)\n"
                  << "  $RYBENCH_CONFIG must point to the config root directory.\n";
        return 1;
    }

    rybench::common::SignalHandler::setup(running);

    const char* configRoot = std::getenv("RYBENCH_CONFIG");
    if (!configRoot) {
        std::cerr << "$RYBENCH_CONFIG is not set\n";
        return 1;
    }

    rybench::common::Config config;
    try {
        config.loadNode(configRoot, argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << "\n";
        return 1;
    }

    return rybench::transport::iceoryx2::run(config, running);
}

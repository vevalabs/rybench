#include "iceoryx2_publisher.hpp"
#include "iceoryx2_consumer.hpp"
#include <rybench/core/engine.hpp>
#include <rybench/core/config.hpp>
#include <rybench/core/signal_handler.hpp>

#include <atomic>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: rybench-iceoryx2 <node-name>  (e.g. publisher@pub1)\n"
                  << "  $RYBENCH_CONFIG must point to the config root directory.\n";
        return 1;
    }

    rybench::core::SignalHandler signals;

    const char* configRoot = std::getenv("RYBENCH_CONFIG");
    if (!configRoot) {
        std::cerr << "$RYBENCH_CONFIG is not set\n";
        return 1;
    }

    rybench::core::Config config;
    try {
        config.loadNode(configRoot, argv[1]);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << "\n";
        return 1;
    }

    rybench::core::Engine engine(config, signals.running());

    std::string mode = config.get("iceoryx2", "mode", "publish");
    if (mode == "consume") {
        rybench::transport::iceoryx2::Iceoryx2Consumer consumer;
        return engine.run(consumer);
    } else if (mode == "publish") {
        rybench::transport::iceoryx2::Iceoryx2Publisher publisher;
        return engine.run(publisher);
    } else {
        std::cerr << "Choose publisher/consumer Unsupported mode:" << mode;
        return 1;
    }
}
    

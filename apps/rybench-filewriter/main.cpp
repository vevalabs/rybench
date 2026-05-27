#include "filewriter_publisher.hpp"
#include <rybench/core/engine.hpp>
#include <rybench/core/config.hpp>
#include <rybench/core/signal_handler.hpp>

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: rybench-filewriter <node-name>  (e.g. filewriter:publisher@)\n"
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
    rybench::transport::filewriter::FilewriterPublisher publisher;
    return engine.run(publisher);
}

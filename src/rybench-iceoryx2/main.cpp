#include "config.hpp"
#include "signal_handler.hpp"
#include "generator_factory.hpp"
#include "serializer_factory.hpp"
#include "iceoryx2_publisher.hpp"

#include <iostream>
#include <thread>
#include <chrono>
#include <atomic>

std::atomic<bool> running{true};

int main(int argc, char* argv[]) {
    std::string configPath = "config/publisher.toml";
    if (argc > 1) {
        configPath = argv[1];
    }

    rybench::common::SignalHandler::setup(running);

    // Load configuration
    rybench::common::Config config;
    try {
        config.load(configPath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        return 1;
    }

    // Create generator
    rybench::generator::GeneratorPtr gen;
    try {
        gen = rybench::generator::createGenerator(config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create generator: " << e.what() << std::endl;
        return 1;
    }
    std::cout << "Generator: " << gen->name() << std::endl;

    // Create serializer
    rybench::serializer::SerializerPtr ser;
    try {
        ser = rybench::serializer::createSerializer(config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create serializer: " << e.what() << std::endl;
        return 1;
    }
    std::cout << "Serializer: " << ser->name() << std::endl;

    // Create publisher (reads [iceoryx2] config section internally)
    rybench::Iceoryx2Publisher publisher;
    publisher.init(config);

    auto interval = std::chrono::milliseconds(1000 / publisher.rateHz());

    std::cout << "Started. Rate: " << publisher.rateHz() << " Hz. Press Ctrl+C to stop." << std::endl;

    uint64_t messageCount = 0;
    while (running) {
        rybench::common::Message msg = gen->generate();

        if (msg.empty()) {
            std::cout << "Generated empty message, skipping." << std::endl;
            continue;
        }

        auto serialized = ser->serialize(msg);
        if (publisher.publish(serialized.data(), serialized.size())) {
            messageCount++;
            if (messageCount % 100 == 0) {
                std::cout << "Published " << messageCount << " messages" << std::endl;
            }
        }

        std::this_thread::sleep_for(interval);
    }

    std::cout << "Shutting down. Total messages: " << messageCount << std::endl;
    publisher.shutdown();

    return 0;
}

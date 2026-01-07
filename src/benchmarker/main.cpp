#include "common/config.hpp"
#include "common/signal_handler.hpp"
#include "serializer/serializer_factory.hpp"
#include "serializer/json_serializer.hpp"
#include "generator/generator_factory.hpp"
#include "publisher/publisher_factory.hpp"

#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>
#include <atomic>

std::atomic<bool> running{true};

int main(int argc, char* argv[]) {
    std::string configPath = "config/publisher.ini";
    if (argc > 1) {
        configPath = argv[1];
    }

    common::SignalHandler::setup(running);

    // Load configuration
    common::Config config;
    try {
        config.load(configPath);
    } catch (const std::exception& e) {
        std::cerr << "Failed to load config: " << e.what() << std::endl;
        return 1;
    }

    // Create serializer (knows the message structure)
    serializer::SerializerPtr ser;
    try {
        ser = serializer::createSerializer(config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create serializer: " << e.what() << std::endl;
        return 1;
    }
    std::cout << "Serializer: " << ser->name() << std::endl;

    // Get schema from serializer and create generator
    common::Schema schema = ser->getSchema();
    generator::GeneratorPtr gen;
    try {
        gen = generator::createGenerator(config, schema);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create generator: " << e.what() << std::endl;
        return 1;
    }
    std::cout << "Generator: " << gen->name() << std::endl;

    // Create publisher
    publisher::PublisherPtr pub;
    try {
        pub = publisher::createPublisher(config);
    } catch (const std::exception& e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    std::cout << "Publisher: " << pub->name() << std::endl;

    // Publish rate
    int rateHz = config.getInt("publisher", "rate_hz", 10);
    auto interval = std::chrono::milliseconds(1000 / rateHz);

    // Dump setup
    bool dumpEnabled = config.get("dump", "enabled", "false") == "true";
    std::string dumpPath = config.get("dump", "output", "dump/benchmark.jsonl");
    std::ofstream dumpFile;
    serializer::JsonSerializer jsonDumper;  // For converting DataValue to JSON
    if (dumpEnabled) {
        dumpFile.open(dumpPath, std::ios::out | std::ios::trunc);
        if (dumpFile.is_open()) {
            std::cout << "Dump: " << dumpPath << std::endl;
        } else {
            std::cerr << "Warning: Failed to open dump file: " << dumpPath << std::endl;
            dumpEnabled = false;
        }
    }

    std::cout << "Started. Rate: " << rateHz << " Hz. Press Ctrl+C to stop." << std::endl;

    uint64_t messageCount = 0;
    while (running) {
        common::DataValue data = gen->generate();

        // Dump to file (JSON format)
        if (dumpEnabled && dumpFile.is_open()) {
            auto jsonBytes = jsonDumper.serialize(data);
            dumpFile.write(reinterpret_cast<const char*>(jsonBytes.data()), jsonBytes.size());
            dumpFile << '\n';
        }

        // Serialize to binary
        std::vector<uint8_t> buffer = ser->serialize(data);

        if (buffer.empty()) {
            std::cout << "Generated empty buffer, skipping." << std::endl;
            return 0;
        }

        // Publish
        if (pub->publish(buffer.data(), buffer.size())) {
            messageCount++;
            if (messageCount % 100 == 0) {
                std::cout << "Published " << messageCount << " messages" << std::endl;
            }
        }

        std::this_thread::sleep_for(interval);
    }

    std::cout << "Shutting down. Total messages: " << messageCount << std::endl;
    pub->shutdown();

    if (dumpFile.is_open()) {
        dumpFile.close();
        std::cout << "Dump file saved: " << dumpPath << std::endl;
    }

    return 0;
}

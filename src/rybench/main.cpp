#include "config.hpp"
#include "signal_handler.hpp"
#include "generator_factory.hpp"
#include "publisher_factory.hpp"
#include "serializer_factory.hpp"

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

    // Create generator
    generator::GeneratorPtr gen;
    try {
        gen = generator::createGenerator(config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create generator: " << e.what() << std::endl;
        return 1;
    }
    std::cout << "Generator: " << gen->name() << std::endl;

    // Create serializer
    serializer::SerializerPtr ser;
    try {
        ser = serializer::createSerializer(config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create serializer: " << e.what() << std::endl;
        return 1;
    }
    std::cout << "Serializer: " << ser->name() << std::endl;

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

    // Dump setup (binary FlatBuffers format)
    bool dumpEnabled = config.get("dump", "enabled", "false") == "true";
    std::string dumpPath = config.get("dump", "output", "dump/benchmark.bin");
    std::ofstream dumpFile;
    if (dumpEnabled) {
        dumpFile.open(dumpPath, std::ios::out | std::ios::binary | std::ios::trunc);
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
        common::Message msg = gen->generate();

        if (msg.empty()) {
            std::cout << "Generated empty message, skipping." << std::endl;
            continue;
        }

        // Dump to file (binary format: size + data)
        if (dumpEnabled && dumpFile.is_open()) {
            uint32_t msgSize = static_cast<uint32_t>(msg.size());
            dumpFile.write(reinterpret_cast<const char*>(&msgSize), sizeof(msgSize));
            dumpFile.write(reinterpret_cast<const char*>(msg.data()), msg.size());
        }

        // Serialize and publish
        auto serialized = ser->serialize(msg);
        if (pub->publish(serialized.data(), serialized.size())) {
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

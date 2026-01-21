#include "config.hpp"
#include "signal_handler.hpp"
#include "generator_factory.hpp"
#include "serializer_factory.hpp"

#include <iostream>
#include <fstream>
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

    // Read filewriter config
    std::string outputPath = config.get("filewriter", "output", "dump/benchmark.bin");
    int rateHz = config.getInt("filewriter", "rate_hz", 10);
    int count = config.getInt("filewriter", "count", 0);  // 0 = unlimited

    // Open output file
    std::ofstream outputFile(outputPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!outputFile.is_open()) {
        std::cerr << "Failed to open output file: " << outputPath << std::endl;
        return 1;
    }
    std::cout << "Output: " << outputPath << std::endl;

    auto interval = std::chrono::milliseconds(1000 / rateHz);

    std::cout << "Started. Rate: " << rateHz << " Hz.";
    if (count > 0) {
        std::cout << " Count: " << count << ".";
    }
    std::cout << " Press Ctrl+C to stop." << std::endl;

    uint64_t messageCount = 0;
    while (running && (count == 0 || messageCount < static_cast<uint64_t>(count))) {
        rybench::common::Message msg = gen->generate();

        if (msg.empty()) {
            std::cout << "Generated empty message, skipping." << std::endl;
            continue;
        }

        // Serialize
        auto serialized = ser->serialize(msg);

        // Write to file (binary format: size + data)
        uint32_t msgSize = static_cast<uint32_t>(serialized.size());
        outputFile.write(reinterpret_cast<const char*>(&msgSize), sizeof(msgSize));
        outputFile.write(reinterpret_cast<const char*>(serialized.data()), serialized.size());

        messageCount++;
        if (messageCount % 100 == 0) {
            std::cout << "Written " << messageCount << " messages" << std::endl;
        }

        std::this_thread::sleep_for(interval);
    }

    outputFile.close();
    std::cout << "Completed. Total messages: " << messageCount << std::endl;
    std::cout << "Output file saved: " << outputPath << std::endl;

    return 0;
}

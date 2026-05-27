#include <rybench/transport/filewriter/filewriter_runner.hpp>
#include <rybench/generator/generator_factory.hpp>
#include <rybench/serializer/serializer_factory.hpp>

#include <iostream>
#include <fstream>
#include <thread>
#include <chrono>

namespace rybench::transport::filewriter {

int run(const common::Config& config, std::atomic<bool>& running) {
    rybench::generator::GeneratorPtr gen;
    try {
        gen = rybench::generator::createGenerator(config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create generator: " << e.what() << "\n";
        return 1;
    }
    std::cout << "Generator: " << gen->name() << "\n";

    rybench::serializer::SerializerPtr ser;
    try {
        ser = rybench::serializer::createSerializer(config);
    } catch (const std::exception& e) {
        std::cerr << "Failed to create serializer: " << e.what() << "\n";
        return 1;
    }
    std::cout << "Serializer: " << ser->name() << "\n";

    std::string outputPath = config.get("filewriter", "output", "dump/benchmark.bin");
    int rateHz = config.getInt("filewriter", "rate_hz", 10);
    int count  = config.getInt("filewriter", "count", 0);

    std::ofstream outputFile(outputPath, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!outputFile.is_open()) {
        std::cerr << "Failed to open output file: " << outputPath << "\n";
        return 1;
    }
    std::cout << "Output: " << outputPath << "\n";

    auto interval = std::chrono::milliseconds(1000 / rateHz);
    std::cout << "Started. Rate: " << rateHz << " Hz.";
    if (count > 0) std::cout << " Count: " << count << ".";
    std::cout << " Press Ctrl+C to stop.\n";

    uint64_t messageCount = 0;
    while (running && (count == 0 || messageCount < static_cast<uint64_t>(count))) {
        rybench::common::Message msg = gen->generate();
        if (msg.empty()) {
            std::cout << "Generated empty message, skipping.\n";
            continue;
        }

        auto serialized = ser->serialize(msg);
        uint32_t msgSize = static_cast<uint32_t>(serialized.size());
        outputFile.write(reinterpret_cast<const char*>(&msgSize), sizeof(msgSize));
        outputFile.write(reinterpret_cast<const char*>(serialized.data()), serialized.size());

        if (++messageCount % 100 == 0) {
            std::cout << "Written " << messageCount << " messages\n";
        }

        std::this_thread::sleep_for(interval);
    }

    outputFile.close();
    std::cout << "Completed. Total messages: " << messageCount << "\n";
    std::cout << "Output file saved: " << outputPath << "\n";
    return 0;
}

} // namespace rybench::transport::filewriter

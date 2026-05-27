#include <rybench/core/engine.hpp>
#include <rybench/generator/generator_factory.hpp>
#include <rybench/serializer/serializer_factory.hpp>

#include <chrono>
#include <iostream>
#include <thread>

namespace rybench::core {

Engine::Engine(const Config& config, std::atomic<bool>& running)
    : config_(config), running_(running) {}

int Engine::run(transport::Publisher& publisher) {
    auto genResult = rybench::generator::createGenerator(config_);
    if (!genResult) {
        std::cerr << "Failed to create generator: " << genResult.error() << "\n";
        return 1;
    }
    auto gen = std::move(*genResult);
    std::cout << "Generator: " << gen->name() << "\n";

    auto serResult = rybench::serializer::createSerializer(config_);
    if (!serResult) {
        std::cerr << "Failed to create serializer: " << serResult.error() << "\n";
        return 1;
    }
    auto ser = std::move(*serResult);
    std::cout << "Serializer: " << ser->name() << "\n";

    if (auto result = publisher.init(config_); !result) {
        std::cerr << result.error() << "\n";
        return 1;
    }

    auto interval = std::chrono::milliseconds(1000 / publisher.rateHz());
    std::cout << "Started. Rate: " << publisher.rateHz() << " Hz. Press Ctrl+C to stop.\n";

    uint64_t messageCount = 0;
    while (running_) {
        Message msg = gen->generate();
        if (msg.empty()) {
            std::cout << "Generated empty message, skipping.\n";
            continue;
        }
        auto serialized = ser->serialize(msg);
        if (publisher.publish(serialized.data(), serialized.size())) {
            if (++messageCount % 100 == 0)
                std::cout << "Published " << messageCount << " messages\n";
        }
        std::this_thread::sleep_for(interval);
    }

    std::cout << "Shutting down. Total messages: " << messageCount << "\n";
    publisher.shutdown();
    return 0;
}

int Engine::run(transport::Consumer& consumer) {
    if (auto result = consumer.init(config_); !result) {
        std::cerr << result.error() << "\n";
        return 1;
    }
    std::cout << "Waiting for messages... Press Ctrl+C to stop.\n";
    consumer.run(running_);
    std::cout << "Shutting down. Total messages: " << consumer.messageCount() << "\n";
    consumer.shutdown();
    return 0;
}

} // namespace rybench::core

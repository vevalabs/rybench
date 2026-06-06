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
    auto genSerResult = rybench::serializer::createSerializer(
        config_, config_.get("generator", "backend", "flatbuffers"));
    if (!genSerResult) {
        std::cerr << "Failed to create generation serializer: " << genSerResult.error() << "\n";
        return 1;
    }
    auto genSer = std::move(*genSerResult);

    auto outSerResult = rybench::serializer::createSerializer(
        config_, config_.get("serializer", "type", "flatbuffers"));
    if (!outSerResult) {
        std::cerr << "Failed to create output serializer: " << outSerResult.error() << "\n";
        return 1;
    }
    auto outSer = std::move(*outSerResult);

    auto genResult = rybench::generator::createGenerator(config_, *genSer);
    if (!genResult) {
        std::cerr << "Failed to create generator: " << genResult.error() << "\n";
        return 1;
    }
    auto gen = std::move(*genResult);

    std::cout << "Generator:          " << gen->name() << "\n";
    std::cout << "Generation backend: " << genSer->name() << "\n";
    std::cout << "Output serializer:  " << outSer->name() << "\n";

    if (auto result = publisher.init(config_); !result) {
        std::cerr << result.error() << "\n";
        return 1;
    }

    const int rate = publisher.rateHz();
    // rate <= 0 (or very high) means "unthrottled" — interval 0 skips the sleep.
    const auto interval = std::chrono::milliseconds(rate > 0 ? 1000 / rate : 0);
    std::cout << "Started. Rate: " << rate << " Hz. Press Ctrl+C to stop.\n";

    uint64_t messageCount = 0;
    while (running_) {
        Message msg = gen->generate();
        if (msg.empty()) {
            std::cout << "Generated empty message, skipping.\n";
            continue;
        }
        auto serialized = outSer->serialize(msg);
        if (publisher.publish(serialized.data(), serialized.size())) {
            if (++messageCount % 1000 == 0)
                std::cout << "Published " << messageCount << " messages\n";
        }
        if (interval.count() > 0)
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

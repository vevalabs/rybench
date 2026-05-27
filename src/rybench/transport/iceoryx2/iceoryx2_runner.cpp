#include <rybench/transport/iceoryx2/iceoryx2_runner.hpp>
#include "iceoryx2_publisher.hpp"
#include "iceoryx2_consumer.hpp"
#include <rybench/generator/generator_factory.hpp>
#include <rybench/serializer/serializer_factory.hpp>

#include <iostream>
#include <thread>
#include <chrono>

namespace rybench::transport::iceoryx2 {

static int runPublisher(const common::Config& config, std::atomic<bool>& running) {
    auto genResult = rybench::generator::createGenerator(config);
    if (!genResult) {
        std::cerr << "Failed to create generator: " << genResult.error() << "\n";
        return 1;
    }
    auto gen = std::move(*genResult);
    std::cout << "Generator: " << gen->name() << "\n";

    auto serResult = rybench::serializer::createSerializer(config);
    if (!serResult) {
        std::cerr << "Failed to create serializer: " << serResult.error() << "\n";
        return 1;
    }
    auto ser = std::move(*serResult);
    std::cout << "Serializer: " << ser->name() << "\n";

    Iceoryx2Publisher publisher;
    if (auto result = publisher.init(config); !result) {
        std::cerr << result.error() << "\n";
        return 1;
    }

    auto interval = std::chrono::milliseconds(1000 / publisher.rateHz());
    std::cout << "Started. Rate: " << publisher.rateHz() << " Hz. Press Ctrl+C to stop.\n";

    uint64_t messageCount = 0;
    while (running) {
        rybench::common::Message msg = gen->generate();
        if (msg.empty()) {
            std::cout << "Generated empty message, skipping.\n";
            continue;
        }
        auto serialized = ser->serialize(msg);
        if (publisher.publish(serialized.data(), serialized.size())) {
            if (++messageCount % 100 == 0) {
                std::cout << "Published " << messageCount << " messages\n";
            }
        }
        std::this_thread::sleep_for(interval);
    }

    std::cout << "Shutting down. Total messages: " << messageCount << "\n";
    publisher.shutdown();
    return 0;
}

static int runConsumer(const common::Config& config, std::atomic<bool>& running) {
    Iceoryx2Consumer consumer;
    if (auto result = consumer.init(config); !result) {
        std::cerr << result.error() << "\n";
        return 1;
    }
    std::cout << "Waiting for messages... Press Ctrl+C to stop.\n";
    consumer.run(running);
    std::cout << "Shutting down. Total messages: " << consumer.messageCount() << "\n";
    consumer.shutdown();
    return 0;
}

int run(const common::Config& config, std::atomic<bool>& running) {
    std::string mode = config.get("iceoryx2", "mode", "publish");
    if (mode == "consume") {
        return runConsumer(config, running);
    }
    return runPublisher(config, running);
}

} // namespace rybench::transport::iceoryx2

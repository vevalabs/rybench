#include <rybench/core/engine.hpp>
#include <rybench/generator/generator_factory.hpp>
#include <rybench/serializer/serializer_factory.hpp>

#include <chrono>
#include <cstdlib>
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
    // Stop after N generated messages; 0 = unlimited (run until Ctrl+C).
    const uint64_t maxCount = static_cast<uint64_t>(config_.getInt("generator", "count", 0));
    std::cout << "Started. Rate: " << rate << " Hz";
    if (maxCount) std::cout << ", count: " << maxCount;
    std::cout << ". Press Ctrl+C to stop.\n";

    const bool bench = std::getenv("RYBENCH_BENCH") != nullptr;
    uint64_t messageCount = 0;

    // Benchmark mode: isolate generation with a tight generate-only loop timed by a
    // SINGLE clock pair around the whole loop — so the per-event steady_clock::now()
    // cost (~15-20 ns) is not folded into the measured time. No serialize/publish.
    // `sink` reads a byte of every message (and is printed) so -O3 cannot elide the
    // generate loop as dead code.
    if (bench) {
        volatile uint64_t sink = 0;
        const auto t0 = std::chrono::steady_clock::now();
        while (running_ && (maxCount == 0 || messageCount < maxCount)) {
            Message msg;
            try {
                msg = gen->generate();
            } catch (const std::exception& e) {
                std::cout << e.what() << "\nGenerator stopped after " << messageCount
                          << " messages.\n";
                break;
            }
            if (msg.empty()) continue;
            sink += msg.data()[0];                 // observable -> defeats dead-code elimination
            ++messageCount;
        }
        const auto genElapsed = std::chrono::steady_clock::now() - t0;
        const double ms   = std::chrono::duration_cast<std::chrono::nanoseconds>(genElapsed).count() / 1e6;
        const double evps = (ms > 0.0) ? messageCount / (ms / 1000.0) : 0.0;
        std::cout << "[bench] gen-only: " << messageCount << " events, "
                  << ms << " ms, " << static_cast<uint64_t>(evps) << " ev/s"
                  << " (sink " << sink << ")\n";
        std::cout << "Shutting down. Total messages: " << messageCount << "\n";
        publisher.shutdown();
        return 0;
    }

    while (running_ && (maxCount == 0 || messageCount < maxCount)) {
        Message msg;
        try {
            msg = gen->generate();
        } catch (const std::exception& e) {
            // A generator that cannot continue (e.g. property_guided hitting an
            // unsatisfiable step) ends the run cleanly with the trace so far.
            std::cout << e.what() << "\nGenerator stopped after " << messageCount
                      << " messages.\n";
            break;
        }
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

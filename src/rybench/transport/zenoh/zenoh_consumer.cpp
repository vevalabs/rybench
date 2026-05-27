#include "zenoh_consumer.hpp"

#include <zenoh.hxx>

#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>

namespace rybench::transport::zenoh {

struct ZenohConsumer::Impl {
    ::zenoh::Session session;
    std::unique_ptr<::zenoh::Subscriber<void>> subscriber;
    std::mutex fileMutex;

    Impl(::zenoh::Session&& s) : session(std::move(s)) {}
};

ZenohConsumer::ZenohConsumer() = default;

ZenohConsumer::~ZenohConsumer() {
    shutdown();
}

void ZenohConsumer::init(const common::Config& config) {
    // Read config from [zenoh] section
    keyExpr_ = config.get("zenoh", "key_expr", "Benchmark/Default/Data");
    outputPath_ = config.get("zenoh", "output", "dump/zenoh-received.bin");

    // Open output file
    outputFile_.open(outputPath_, std::ios::binary | std::ios::trunc);
    if (!outputFile_.is_open()) {
        std::cerr << "[Zenoh] Failed to open output file: " << outputPath_ << "\n";
        return;
    }

    try {
        // Open Zenoh session
        ::zenoh::Config zconfig = ::zenoh::Config::create_default();
        auto session = ::zenoh::Session::open(std::move(zconfig));

        impl_ = std::make_unique<Impl>(std::move(session));

        // Create subscriber with callback
        auto callback = [this](::zenoh::Sample& sample) {
            const auto& payload = sample.get_payload();
            auto data = payload.as_vector();

            std::lock_guard<std::mutex> lock(impl_->fileMutex);

            // Write to file: size + data
            uint32_t msgSize = static_cast<uint32_t>(data.size());
            outputFile_.write(reinterpret_cast<const char*>(&msgSize), sizeof(msgSize));
            outputFile_.write(reinterpret_cast<const char*>(data.data()), msgSize);

            messageCount_++;
            if (messageCount_ % 100 == 0) {
                std::cout << "Received " << messageCount_.load() << " messages" << std::endl;
            }
        };

        auto on_drop = []() {};

        impl_->subscriber = std::make_unique<::zenoh::Subscriber<void>>(
            impl_->session.declare_subscriber(::zenoh::KeyExpr(keyExpr_), callback, on_drop)
        );

        initialized_ = true;

        std::cout << "[Zenoh] Consumer initialized: " << keyExpr_ << "\n";
        std::cout << "[Zenoh] Writing to: " << outputPath_ << "\n";

    } catch (const std::exception& e) {
        std::cerr << "[Zenoh] Failed to initialize: " << e.what() << std::endl;
    }
}

void ZenohConsumer::run(std::atomic<bool>& running) {
    if (!initialized_ || !impl_) {
        return;
    }

    // Just wait - callback handles messages
    while (running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void ZenohConsumer::shutdown() {
    if (initialized_) {
        impl_.reset();
        if (outputFile_.is_open()) {
            outputFile_.close();
        }
        std::cout << "[Zenoh] Consumer shutdown. Received " << messageCount_.load() << " messages.\n";
        std::cout << "[Zenoh] Saved to: " << outputPath_ << "\n";
        initialized_ = false;
    }
}

} // namespace rybench::transport::zenoh

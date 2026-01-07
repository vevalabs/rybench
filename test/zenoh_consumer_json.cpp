#include <zenoh.hxx>
#include <iostream>
#include <atomic>
#include <csignal>
#include <string>
#include <thread>
#include <chrono>

std::atomic<bool> running{true};

void signalHandler(int) {
    running = false;
}

int main() {
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        // Open Zenoh session
        zenoh::Config config = zenoh::Config::create_default();
        auto session = zenoh::Session::open(std::move(config));

        // Subscribe to key expression: service/instance/event
        std::string keyExpr = "Benchmark/Default/Data";

        uint64_t messageCount = 0;

        auto callback = [&messageCount](zenoh::Sample& sample) {
            messageCount++;
            const auto& payload = sample.get_payload();
            auto data = payload.as_vector();
            std::string msg(reinterpret_cast<const char*>(data.data()), data.size());
            std::cout << "[" << messageCount << "] " << msg << std::endl;
        };

        auto on_drop = []() {};

        auto subscriber = session.declare_subscriber(zenoh::KeyExpr(keyExpr), callback, on_drop);

        std::cout << "Zenoh consumer started. Subscribing to: " << keyExpr << std::endl;
        std::cout << "Waiting for messages... (Ctrl+C to stop)" << std::endl;

        while (running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\nTotal messages received: " << messageCount << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}

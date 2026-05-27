#pragma once

#include <atomic>
#include <csignal>

namespace rybench {
namespace common {

class SignalHandler {
public:
    static void setup(std::atomic<bool>& running) {
        g_running_ = &running;
        std::signal(SIGINT, handler);
        std::signal(SIGTERM, handler);
    }

private:
    static inline std::atomic<bool>* g_running_ = nullptr;

    static void handler(int) {
        if (g_running_) {
            g_running_->store(false);
        }
    }
};

} // namespace common
} // namespace rybench

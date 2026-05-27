#pragma once

#include <atomic>
#include <csignal>

namespace rybench::core {

class SignalHandler {
public:
    SignalHandler() {
        g_running_ = &running_;
        std::signal(SIGINT,  handler);
        std::signal(SIGTERM, handler);
    }

    ~SignalHandler() {
        g_running_ = nullptr;
        std::signal(SIGINT,  SIG_DFL);
        std::signal(SIGTERM, SIG_DFL);
    }

    std::atomic<bool>& running() { return running_; }

private:
    std::atomic<bool> running_{true};

    static inline std::atomic<bool>* g_running_ = nullptr;

    static void handler(int) {
        if (g_running_)
            g_running_->store(false);
    }
};

} // namespace rybench::core

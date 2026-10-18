#include "zenoh_consumer.hpp"
#include "monitor_sink.hpp"
#include "monpoly_sink.hpp"

#include <zenoh.hxx>

#include <iostream>
#include <thread>
#include <chrono>
#include <mutex>
#include <cstring>

namespace rybench::transport::zenoh {

struct ZenohConsumer::Impl {
    ::zenoh::Session session;
    std::unique_ptr<::zenoh::Subscriber<void>> subscriber;          // file mode (callback)
    // Monitor mode: FIFO-channel handler drained in OUR thread (see run()), so the
    // decode+monitor runs with hot reelay state instead of inside zenoh's callback thread.
    std::unique_ptr<::zenoh::Subscriber<::zenoh::channels::FifoHandler<::zenoh::Sample>>> monSub;
    std::mutex fileMutex;

    Impl(::zenoh::Session&& s) : session(std::move(s)) {}
};

ZenohConsumer::ZenohConsumer() = default;

ZenohConsumer::~ZenohConsumer() {
    shutdown();
}

std::expected<void, std::string> ZenohConsumer::init(const core::Config& config) {
    keyExpr_ = config.get("zenoh", "key_expr", "Benchmark/Default/Data");
    outputPath_ = config.get("zenoh", "output", "dump/zenoh-received.bin");

    // reelay decodes the wire bytes itself (needs monitor_format + [schema] bfbs);
    // monpoly takes the wire bytes as-is -- they are already MonpolySerializer-
    // encoded facts on the publish side, so no decode step and no bfbs needed here.
    if (config.get("zenoh", "monitor", "false") == "true") {
        const std::string spec    = config.get("zenoh", "monitor_spec", "");
        const std::string backend = config.get("zenoh", "monitor_backend", "reelay");
        if (spec.empty())
            return std::unexpected("[Zenoh] monitor mode needs monitor_spec");
        try {
            if (backend == "monpoly") {
                const std::string bin = config.get("zenoh", "monitor_monpoly_bin", "monpoly");
                monitorSink_ = std::make_unique<monitor::MonpolySink>(spec, bin);
            } else {
                const std::string fmt  = config.get("zenoh", "monitor_format", "flatbuffers");
                const std::string bfbs = config.get("schema", "bfbs", "");
                if (bfbs.empty())
                    return std::unexpected("[Zenoh] monitor_backend=reelay needs [schema] bfbs");
                monitorSink_ = std::make_unique<monitor::MonitorSink>(fmt, bfbs, spec);
            }
        } catch (const std::exception& e) {
            return std::unexpected(std::string("[Zenoh] Failed to start monitor backend '")
                                    + backend + "': " + e.what());
        }
        monLatency_ = (config.get("zenoh", "monitor_latency", "false") == "true");
        std::cout << "[Zenoh] Monitor mode: backend=" << backend
                  << " spec=" << spec << (monLatency_ ? " (latency)" : "") << "\n";
    }

    if (!monitorSink_) {
        outputFile_.open(outputPath_, std::ios::binary | std::ios::trunc);
        if (!outputFile_.is_open())
            return std::unexpected("[Zenoh] Failed to open output file: " + outputPath_);
    }

    try {
        ::zenoh::Config zconfig = ::zenoh::Config::create_default();
        auto session = ::zenoh::Session::open(std::move(zconfig));

        impl_ = std::make_unique<Impl>(std::move(session));

        if (monitorSink_) {
            // Handler-based subscriber: samples land in a FIFO we drain ourselves in
            // run(), keeping the monitor off zenoh's callback thread.
            impl_->monSub = std::make_unique<
                ::zenoh::Subscriber<::zenoh::channels::FifoHandler<::zenoh::Sample>>>(
                impl_->session.declare_subscriber(::zenoh::KeyExpr(keyExpr_),
                                                  ::zenoh::channels::FifoChannel(65536)));
        } else {
            auto callback = [this](::zenoh::Sample& sample) {
                const auto& payload = sample.get_payload();
                auto data = payload.as_vector();
                uint32_t msgSize = static_cast<uint32_t>(data.size());

                std::lock_guard<std::mutex> lock(impl_->fileMutex);
                outputFile_.write(reinterpret_cast<const char*>(&msgSize), sizeof(msgSize));
                outputFile_.write(reinterpret_cast<const char*>(data.data()), msgSize);

                messageCount_++;
                if (messageCount_ % 100 == 0) {
                    std::cout << "Received " << messageCount_.load() << " messages\n";
                }
            };

            auto on_drop = []() {};

            impl_->subscriber = std::make_unique<::zenoh::Subscriber<void>>(
                impl_->session.declare_subscriber(::zenoh::KeyExpr(keyExpr_), callback, on_drop)
            );
        }

        initialized_ = true;

        std::cout << "[Zenoh] Consumer initialized: " << keyExpr_ << "\n";
        if (!monitorSink_)
            std::cout << "[Zenoh] Writing to: " << outputPath_ << "\n";
        return {};
    } catch (const std::exception& e) {
        return std::unexpected(std::string("[Zenoh] Failed to initialize: ") + e.what());
    }
}

void ZenohConsumer::run(std::atomic<bool>& running) {
    if (!initialized_ || !impl_) {
        return;
    }

    // Monitor mode: drain the FIFO in this thread (busy-poll, mirrors the iceoryx2
    // consumer). The monitor's decode+update then runs here with reelay state hot in
    // cache, instead of inside zenoh's callback thread where it gets inflated by
    // callback-thread jitter and cache eviction.
    if (monitorSink_ && impl_->monSub) {
        while (running) {
            auto res = impl_->monSub->handler().try_recv();
            if (std::holds_alternative<::zenoh::Sample>(res)) {
                const auto& sample = std::get<::zenoh::Sample>(res);
                auto data = sample.get_payload().as_vector();
                uint32_t msgSize = static_cast<uint32_t>(data.size());
                if (!monStarted_) {
                    monStart_ = std::chrono::steady_clock::now();
                    monLast_ = monStart_;
                    monStarted_ = true;
                }
                if (monLatency_ && msgSize >= sizeof(uint64_t)) {
                    uint64_t sendNs;
                    std::memcpy(&sendNs, data.data(), sizeof(sendNs));
                    monitorSink_->feedBreakdown(data.data() + sizeof(sendNs),
                                                msgSize - sizeof(uint64_t), sendNs);
                } else {
                    monitorSink_->feed(data.data(), msgSize);
                    if ((messageCount_.load() & 0x3FF) == 0)
                        monLast_ = std::chrono::steady_clock::now();
                }
                messageCount_.fetch_add(1);
            } else if (std::get<::zenoh::channels::RecvError>(res)
                       == ::zenoh::channels::RecvError::Z_DISCONNECTED) {
                break;
            }
        }
        return;
    }

    // File mode: callback handles messages, just wait.
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
        // Wait for every fed event to resolve (a future-time MonPoly formula resolves
        // asynchronously, well after feed() returns) before reading any stats below.
        if (monitorSink_) monitorSink_->finish();
        if (monitorSink_ && monLatency_) {
            auto s = monitorSink_->latencyStats();
            std::cout << "[Zenoh] Monitor latency (ns): n=" << s.n
                      << " p50=" << s.p50 << " p90=" << s.p90 << " p99=" << s.p99
                      << " max=" << s.max << " mean=" << static_cast<long long>(s.mean)
                      << " true=" << monitorSink_->trueRatio() << "%\n";
            auto tr = monitorSink_->transpStats(), pa = monitorSink_->parseStats(),
                 mo = monitorSink_->monStats();
            std::cout << "[Zenoh] Latency breakdown p50 (ns): transport=" << tr.p50
                      << " parse=" << pa.p50 << " monitor=" << mo.p50 << "\n";
        } else if (monitorSink_ && monStarted_) {
            double ms = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            monLast_ - monStart_).count() / 1e6;
            double eps = ms > 0 ? monitorSink_->count() / (ms / 1000.0) : 0;
            std::cout << "[Zenoh] Monitor: events=" << monitorSink_->count()
                      << " ms=" << ms << " events/s=" << static_cast<long long>(eps)
                      << " true=" << monitorSink_->trueRatio() << "%\n";
        } else {
            std::cout << "[Zenoh] Saved to: " << outputPath_ << "\n";
        }
        initialized_ = false;
    }
}

} // namespace rybench::transport::zenoh

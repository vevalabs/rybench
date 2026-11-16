#include "iceoryx2_consumer.hpp"
#include "monitor_sink.hpp"
#include "monpoly_sink.hpp"

#include <iox2/iceoryx2.hpp>
#include <rybench/serializer/serializer_factory.hpp>
#include <rybench/core/message.hpp>

#include <iostream>
#include <span>
#include <cstring>

namespace rybench::transport::iceoryx2 {


struct Iceoryx2Consumer::Impl {
    iox2::Node<iox2::ServiceType::Ipc> node;
    iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<uint8_t>, void> subscriber;

    Impl(iox2::Node<iox2::ServiceType::Ipc>&& n,
         iox2::Subscriber<iox2::ServiceType::Ipc, iox2::bb::Slice<uint8_t>, void>&& s)
        : node(std::move(n)), subscriber(std::move(s)) {}
};

Iceoryx2Consumer::Iceoryx2Consumer() = default;

Iceoryx2Consumer::~Iceoryx2Consumer() {
    shutdown();
}

std::expected<void, std::string> Iceoryx2Consumer::init(const core::Config& config) {
    service_ = config.get("iceoryx2", "service", "Benchmark");
    instance_ = config.get("iceoryx2", "instance", "Default");
    event_ = config.get("iceoryx2", "event", "Data");
    outputPath_ = config.get("iceoryx2", "output", "dump/iceoryx2-received.bin");
    writeJson_ = (config.get("iceoryx2", "output_json", "false") == "true");

    const std::string serviceName = service_ + "/" + instance_ + "/" + event_;

    // Online monitor mode: feed each received message to a monitor backend. reelay
    // decodes the wire bytes itself (needs monitor_format + [schema] bfbs); monpoly
    // takes the wire bytes as-is -- they are already MonpolySerializer-encoded facts
    // on the publish side, so no decode step and no bfbs needed here.
    if (config.get("iceoryx2", "monitor", "false") == "true") {
        const std::string spec    = config.get("iceoryx2", "monitor_spec", "");
        const std::string backend = config.get("iceoryx2", "monitor_backend", "reelay");
        if (spec.empty())
            return std::unexpected("[Iceoryx2] monitor mode needs monitor_spec");
        try {
            if (backend == "monpoly") {
                const std::string bin = config.get("iceoryx2", "monitor_monpoly_bin", "monpoly");
                const bool spin = (config.get("iceoryx2", "monitor_busy_poll", "false") == "true");
                monitorSink_ = std::make_unique<monitor::MonpolySink>(spec, bin, spin);
            } else {
                const std::string fmt  = config.get("iceoryx2", "monitor_format", "flatbuffers");
                const std::string bfbs = config.get("schema", "bfbs", "");
                if (bfbs.empty())
                    return std::unexpected("[Iceoryx2] monitor_backend=reelay needs [schema] bfbs");
                monitorSink_ = std::make_unique<monitor::MonitorSink>(fmt, bfbs, spec);
            }
        } catch (const std::exception& e) {
            return std::unexpected(std::string("[Iceoryx2] Failed to start monitor backend '")
                                    + backend + "': " + e.what());
        }
        monLatency_ = (config.get("iceoryx2", "monitor_latency", "false") == "true");
        monProc_ = (config.get("iceoryx2", "monitor_proc", "false") == "true");
        std::cout << "[Iceoryx2] Monitor mode: backend=" << backend
                  << " spec=" << spec << (monLatency_ ? " (latency)" : "") << "\n";
    }

    if (!monitorSink_ && writeJson_) {
        auto ser = serializer::createSerializer(config, "json");   // loads [schema] bfbs
        if (!ser)
            return std::unexpected("[Iceoryx2] Failed to create JSON serializer: " + ser.error());
        jsonSer_ = std::move(*ser);
    }

    if (!monitorSink_) {
        outputFile_.open(outputPath_, writeJson_ ? std::ios::trunc
                                                 : (std::ios::binary | std::ios::trunc));
        if (!outputFile_.is_open())
            return std::unexpected("[Iceoryx2] Failed to open output file: " + outputPath_);
    }

    auto node_result = iox2::NodeBuilder{}.create<iox2::ServiceType::Ipc>();
    if (!node_result) return std::unexpected("[Iceoryx2] Failed to create node");

    auto node = std::move(node_result.value());

    auto service_name = iox2::ServiceName::create(serviceName.c_str());
    if (!service_name) return std::unexpected("[Iceoryx2] Invalid service name: " + serviceName);

    auto service_result =
        node.service_builder(service_name.value())
            .publish_subscribe<iox2::bb::Slice<uint8_t>>()
            .open_or_create();

    if (!service_result) return std::unexpected("[Iceoryx2] Failed to open/create service: " + serviceName);

    auto service = std::move(service_result.value());

    auto sub_result = service.subscriber_builder().create();
    if (!sub_result) return std::unexpected("[Iceoryx2] Failed to create subscriber");

    impl_ = std::make_unique<Impl>(std::move(node), std::move(sub_result.value()));
    initialized_ = true;

    std::cout << "[Iceoryx2] Consumer initialized: " << serviceName << "\n";
    if (!monitorSink_)
        std::cout << "[Iceoryx2] Writing to: " << outputPath_ << "\n";
    return {};
}

void Iceoryx2Consumer::run(std::atomic<bool>& running) {
    if (!initialized_ || !impl_) {
        return;
    }

    // Monitor mode: tight busy-poll (no 10ms wait) so the consumer drains at its
    // true decode+monitor rate rather than queue-per-cycle. Timestamp only every
    // 1024 messages to keep now() off the per-message hot path.
    if (monitorSink_) {
        while (running) {
            auto sample = impl_->subscriber.receive().value();
            while (sample.has_value()) {
                auto payload = sample->payload();
                uint32_t n = static_cast<uint32_t>(payload.number_of_elements());
                if (!monStarted_) {
                    monStart_ = std::chrono::steady_clock::now();
                    monLast_ = monStart_;
                    monStarted_ = true;
                }
                if (monProc_) {
                    // receive->verdict: reelay's own decode+monitor cost (no transport).
                    monitorSink_->feedTimed(payload.data(), n);
                    if ((messageCount_ & 0x3FF) == 0)
                        monLast_ = std::chrono::steady_clock::now();
                } else if (monLatency_ && n >= sizeof(uint64_t)) {
                    // [8-byte send ns][message]: full stage breakdown (transport/parse/
                    // monitor) plus end-to-end publish->verdict latency.
                    uint64_t sendNs;
                    std::memcpy(&sendNs, payload.data(), sizeof(sendNs));
                    monitorSink_->feedBreakdown(payload.data() + sizeof(sendNs),
                                                n - sizeof(uint64_t), sendNs);
                } else {
                    monitorSink_->feed(payload.data(), n);
                    if ((messageCount_ & 0x3FF) == 0)
                        monLast_ = std::chrono::steady_clock::now();
                }
                ++messageCount_;
                sample = impl_->subscriber.receive().value();
            }
        }
        return;
    }

    constexpr auto CYCLE_TIME = iox2::bb::Duration::from_millis(10);

    while (running && impl_->node.wait(CYCLE_TIME).has_value()) {
        auto sample = impl_->subscriber.receive().value();
        while (sample.has_value()) {
            auto payload = sample->payload();

            uint32_t msgSize = static_cast<uint32_t>(payload.number_of_elements());
            if (writeJson_) {
                // Deserialize the received FlatBuffer to JSON, one object per line.
                core::Message msg(std::span<const uint8_t>(payload.data(), msgSize),
                                  core::MessageFormat::FlatBuffers);
                auto json = jsonSer_->serialize(msg);
                outputFile_.write(reinterpret_cast<const char*>(json.data()), json.size());
                outputFile_.put('\n');
            } else {
                // Raw: 4-byte size + data.
                outputFile_.write(reinterpret_cast<const char*>(&msgSize), sizeof(msgSize));
                outputFile_.write(reinterpret_cast<const char*>(payload.data()), msgSize);
            }

            messageCount_++;
            if (!monitorSink_ && messageCount_ % 100 == 0) {
                std::cout << "Received " << messageCount_ << " messages" << std::endl;
            }

            sample = impl_->subscriber.receive().value();
        }
    }
}

void Iceoryx2Consumer::shutdown() {
    if (initialized_) {
        impl_.reset();
        if (outputFile_.is_open()) {
            outputFile_.close();
        }
        std::cout << "[Iceoryx2] Consumer shutdown. Received " << messageCount_ << " messages.\n";
        // Wait for every fed event to resolve (a future-time MonPoly formula resolves
        // asynchronously, well after feed() returns) before reading any stats below.
        if (monitorSink_) monitorSink_->finish();
        if (monitorSink_ && monProc_) {
            auto s = monitorSink_->procStats();
            std::cout << "[Iceoryx2] Reelay proc latency (ns): n=" << s.n
                      << " p50=" << s.p50 << " p99=" << s.p99 << " max=" << s.max
                      << " mean=" << static_cast<long long>(s.mean) << "\n";
        } else if (monitorSink_ && monLatency_) {
            auto s = monitorSink_->latencyStats();
            std::cout << "[Iceoryx2] Monitor latency (ns): n=" << s.n
                      << " p50=" << s.p50 << " p90=" << s.p90 << " p99=" << s.p99
                      << " max=" << s.max << " mean=" << static_cast<long long>(s.mean)
                      << " true=" << monitorSink_->trueRatio() << "%\n";
            auto tr = monitorSink_->transpStats(), pa = monitorSink_->parseStats(),
                 mo = monitorSink_->monStats();
            std::cout << "[Iceoryx2] Latency breakdown p50 (ns): transport=" << tr.p50
                      << " parse=" << pa.p50 << " monitor=" << mo.p50 << "\n";
        } else if (monitorSink_ && monStarted_) {
            double ms = std::chrono::duration_cast<std::chrono::nanoseconds>(
                            monLast_ - monStart_).count() / 1e6;
            double eps = ms > 0 ? monitorSink_->count() / (ms / 1000.0) : 0;
            std::cout << "[Iceoryx2] Monitor: events=" << monitorSink_->count()
                      << " ms=" << ms << " events/s=" << static_cast<long long>(eps)
                      << " true=" << monitorSink_->trueRatio() << "%\n";
        } else {
            std::cout << "[Iceoryx2] Saved to: " << outputPath_ << "\n";
        }
        initialized_ = false;
    }
}

} // namespace rybench::transport::iceoryx2

#pragma once
// MonitorSink — feed a Reelay past-time discrete monitor from decoded atoms. Shared
// by the offline harness (rybench-monitor) and the online transport consumers, so
// decode + monitor is identical everywhere. Header-only and inline: no extra
// per-event cost beyond decode + reelay's own update(). Any TU that includes this
// must link rybench + reelay (see apps/rybench-monitor/CMakeLists.txt for the
// include/link recipe).

#include "atom_decoder.hpp"

#include "reelay/json.hpp"
#include "reelay/monitors.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// Reelay reads fields off the input type via these specializations — plain member
// reads, no string conversion.
namespace reelay {
template<> struct timefield<int64_t, rybench::monitor::Event> {
    using input_t = rybench::monitor::Event;
    inline static int64_t get_time(const input_t& c) { return c.time; }
};
template<> struct datafield<rybench::monitor::Event> {
    using input_t = rybench::monitor::Event;
    inline static bool contains(const input_t&, const std::string& k) {
        return k == "p" || k == "q" || k == "r" || k == "s";
    }
    inline static bool as_bool(const input_t& c, const std::string& k) {
        if (k == "p") return c.p;
        if (k == "q") return c.q;
        if (k == "r") return c.r;
        if (k == "s") return c.s;
        return false;
    }
    inline static int64_t as_integer(const input_t&, const std::string&) { return 0; }
    inline static double  as_floating(const input_t&, const std::string&) { return 0; }
    inline static std::string as_string(const input_t&, const std::string&) { return ""; }
    inline static bool contains(const input_t&, std::size_t) { return false; }
    inline static bool as_bool(const input_t&, std::size_t) { return false; }
    inline static int64_t as_integer(const input_t&, std::size_t) { return 0; }
    inline static double  as_floating(const input_t&, std::size_t) { return 0; }
    inline static std::string as_string(const input_t&, std::size_t) { return ""; }
};
}  // namespace reelay

namespace rybench::monitor {

class MonitorSink : public IMonitorSink {
public:
    // fmt: json|flatbuffers|protobuf; bfbs: schema path; spec: reelay past-time MTL.
    MonitorSink(const std::string& fmt, const std::string& bfbs, const std::string& spec)
        : decoder_(fmt, bfbs) {
        auto opts =
            reelay::discrete_timed<int64_t>::monitor<Event, reelay::json>::options();
        monitor_ = reelay::make_monitor(spec, opts);
    }

    // Decode one serialized message and advance the monitor by one step. Track the
    // verdict: with condensing, update() only emits on change, so we hold the last
    // value and count it per event → fraction of events where the spec holds.
    void feed(const uint8_t* payload, uint32_t size) override {
        Event e;
        e.time = static_cast<int64_t>(count_);
        decoder_.decode(payload, size, e);
        auto r = monitor_.update(e);
        if (r.is_object()) {
            auto it = r.find("value");
            if (it != r.end() && it->is_boolean()) lastVerdict_ = it->template get<bool>();
        }
        if (lastVerdict_) ++trueCount_;
        ++count_;
    }

    uint64_t count() const override { return count_; }
    double trueRatio() const override { return count_ ? 100.0 * trueCount_ / count_ : 0.0; }

    // Reelay processing latency: time the decode+monitor of ONE event (receive->verdict).
    // Transport/queue/drop are outside this span — this is reelay's own per-event cost,
    // congestion-independent, so it can be measured at any rate.
    void feedTimed(const uint8_t* payload, uint32_t size) override {
        auto t0 = std::chrono::steady_clock::now();
        feed(payload, size);
        auto t1 = std::chrono::steady_clock::now();
        proc_.push_back(static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }

    // Full stage breakdown for one event, given the publisher's send ns:
    //   transport = incoming - send  |  parse = decode  |  monitor = update  |  e2e = verdict - send
    void feedBreakdown(const uint8_t* payload, uint32_t size, uint64_t sendNs) override {
        auto t0 = std::chrono::steady_clock::now();          // incoming (start of processing)
        uint64_t inNs = static_cast<uint64_t>(t0.time_since_epoch().count());
        Event e;
        e.time = static_cast<int64_t>(count_);
        decoder_.decode(payload, size, e);
        auto t1 = std::chrono::steady_clock::now();          // after parse
        auto r = monitor_.update(e);
        if (r.is_object()) {
            auto it = r.find("value");
            if (it != r.end() && it->is_boolean()) lastVerdict_ = it->template get<bool>();
        }
        if (lastVerdict_) ++trueCount_;
        auto t2 = std::chrono::steady_clock::now();          // after monitor (verdict)
        uint64_t verdNs = static_cast<uint64_t>(t2.time_since_epoch().count());
        auto span = [](auto a, auto b) {
            return static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
        };
        if (inNs   >= sendNs && inNs   - sendNs < 0xffffffffULL) transp_.push_back(static_cast<uint32_t>(inNs   - sendNs));
        parse_.push_back(span(t0, t1));
        mon_.push_back(span(t1, t2));
        if (verdNs >= sendNs && verdNs - sendNs < 0xffffffffULL) lat_.push_back(static_cast<uint32_t>(verdNs - sendNs));
        ++count_;
    }

    LatStats latencyStats() override { return computeStats(lat_); }    // publish->verdict (end-to-end)
    LatStats procStats()    override { return computeStats(proc_); }   // receive->verdict (reelay only)
    LatStats transpStats()  override { return computeStats(transp_); } // send->incoming (transport/net)
    LatStats parseStats()   override { return computeStats(parse_); }  // decode
    LatStats monStats()     override { return computeStats(mon_); }    // monitor.update

private:
    AtomDecoder decoder_;
    reelay::monitor<Event, reelay::json> monitor_;
    uint64_t count_ = 0;
    uint64_t trueCount_ = 0;       // events where the monitor verdict is true
    bool lastVerdict_ = false;     // held between condensed emits
    std::vector<uint32_t> lat_;    // publish->verdict samples (ns)
    std::vector<uint32_t> proc_;   // receive->verdict (reelay decode+monitor) samples (ns)
    std::vector<uint32_t> transp_; // send->incoming (transport) samples (ns)
    std::vector<uint32_t> parse_;  // decode samples (ns)
    std::vector<uint32_t> mon_;    // monitor.update samples (ns)
};

}  // namespace rybench::monitor

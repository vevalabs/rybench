#pragma once
// MonitorSink — decode a serialized message (json|flatbuffers|protobuf) and feed a
// Reelay past-time discrete monitor. Shared by the offline harness (rybench-monitor)
// and the online transport consumers, so decode + monitor is identical everywhere.
// Header-only and inline: no virtual layer, no extra per-event cost beyond decode +
// reelay's own update(). Any TU that includes this must link rybench + reelay (see
// apps/rybench-monitor/CMakeLists.txt for the include/link recipe).

#include "reelay/json.hpp"
#include "reelay/monitors.hpp"

#include "flatbuffers_schema.hpp"
#include "protobuf_serializer.hpp"

#include <flatbuffers/reflection.h>
#include <rapidjson/document.h>
#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rybench::monitor {

// A decoded event: logical time (assigned by receive order) + the boolean atoms.
struct Event {
    int64_t time = 0;
    bool p = false, q = false, r = false, s = false;
};

}  // namespace rybench::monitor

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

inline void setAtom(Event& e, const std::string& n, bool v) {
    if (n == "p") e.p = v;
    else if (n == "q") e.q = v;
    else if (n == "r") e.r = v;
    else if (n == "s") e.s = v;
}

class MonitorSink {
public:
    // fmt: json|flatbuffers|protobuf; bfbs: schema path; spec: reelay past-time MTL.
    MonitorSink(const std::string& fmt, const std::string& bfbs, const std::string& spec)
        : fmt_(fmt) {
        if (fmt_ == "flatbuffers") {
            fbs_.loadFromBfbs(bfbs);
            fbRoot_ = fbs_.reflectionSchema()->root_table();
        } else if (fmt_ == "protobuf") {
            pb_ = std::make_unique<serializer::ProtobufSerializer>(bfbs);
            pbmsg_.reset(pb_->newRootInstanceForTest());
        }
        auto opts =
            reelay::discrete_timed<int64_t>::monitor<Event, reelay::json>::options();
        monitor_ = reelay::make_monitor(spec, opts);
    }

    // Decode one serialized message and advance the monitor by one step. Track the
    // verdict: with condensing, update() only emits on change, so we hold the last
    // value and count it per event → fraction of events where the spec holds.
    inline void feed(const uint8_t* payload, uint32_t size) {
        Event e;
        e.time = static_cast<int64_t>(count_);
        decode(payload, size, e);
        auto r = monitor_.update(e);
        if (r.is_object()) {
            auto it = r.find("value");
            if (it != r.end() && it->is_boolean()) lastVerdict_ = it->template get<bool>();
        }
        if (lastVerdict_) ++trueCount_;
        ++count_;
    }

    uint64_t count() const { return count_; }
    double trueRatio() const { return count_ ? 100.0 * trueCount_ / count_ : 0.0; }

    // End-to-end latency mode: record one publish->verdict sample (ns).
    inline void recordLatency(uint64_t ns) {
        if (ns < 0xffffffffULL) lat_.push_back(static_cast<uint32_t>(ns));
    }
    // Reelay processing latency: time the decode+monitor of ONE event (receive->verdict).
    // Transport/queue/drop are outside this span — this is reelay's own per-event cost,
    // congestion-independent, so it can be measured at any rate.
    inline void feedTimed(const uint8_t* payload, uint32_t size) {
        auto t0 = std::chrono::steady_clock::now();
        feed(payload, size);
        auto t1 = std::chrono::steady_clock::now();
        proc_.push_back(static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }

    // Full stage breakdown for one event, given the publisher's send ns:
    //   transport = incoming - send  |  parse = decode  |  monitor = update  |  e2e = verdict - send
    inline void feedBreakdown(const uint8_t* payload, uint32_t size, uint64_t sendNs) {
        auto t0 = std::chrono::steady_clock::now();          // incoming (start of processing)
        uint64_t inNs = static_cast<uint64_t>(t0.time_since_epoch().count());
        Event e;
        e.time = static_cast<int64_t>(count_);
        decode(payload, size, e);
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
    struct LatStats { uint64_t p50 = 0, p90 = 0, p99 = 0, max = 0; double mean = 0; size_t n = 0; };
    static LatStats stats(std::vector<uint32_t>& v) {
        LatStats st;
        st.n = v.size();
        if (v.empty()) return st;
        std::sort(v.begin(), v.end());
        double sum = 0; for (uint32_t x : v) sum += x;
        st.mean = sum / v.size();
        st.p50 = v[v.size() * 50 / 100];
        st.p90 = v[v.size() * 90 / 100];
        st.p99 = v[v.size() * 99 / 100];
        st.max = v.back();
        return st;
    }
    LatStats latencyStats() { return stats(lat_); }    // publish->verdict (end-to-end)
    LatStats procStats()    { return stats(proc_); }   // receive->verdict (reelay only)
    LatStats transpStats()  { return stats(transp_); } // send->incoming (transport/net)
    LatStats parseStats()   { return stats(parse_); }  // decode
    LatStats monStats()     { return stats(mon_); }    // monitor.update

private:
    inline void decode(const uint8_t* p, uint32_t n, Event& e) {
        if (fmt_ == "json") {
            rapidjson::Document d;
            d.Parse(reinterpret_cast<const char*>(p), n);
            for (auto it = d.MemberBegin(); it != d.MemberEnd(); ++it) {
                const char* name = it->name.GetString();
                if (name[0] && name[1] == '\0' &&
                    (name[0]=='p'||name[0]=='q'||name[0]=='r'||name[0]=='s')) {
                    bool v = it->value.IsBool() ? it->value.GetBool()
                           : it->value.IsInt()  ? it->value.GetInt() != 0
                                                : false;
                    setAtom(e, name, v);
                }
            }
        } else if (fmt_ == "flatbuffers") {
            const auto* t = flatbuffers::GetAnyRoot(p);
            for (const auto* fld : *fbRoot_->fields()) {
                if (fld->type()->base_type() == reflection::BaseType::Bool)
                    setAtom(e, fld->name()->str(),
                            flatbuffers::GetAnyFieldI(*t, *fld) != 0);
            }
        } else {  // protobuf
            pbmsg_->Clear();
            pbmsg_->ParseFromArray(p, static_cast<int>(n));
            const auto* desc = pbmsg_->GetDescriptor();
            const auto* refl = pbmsg_->GetReflection();
            for (int i = 0; i < desc->field_count(); ++i) {
                const auto* fld = desc->field(i);
                if (fld->type() == google::protobuf::FieldDescriptor::TYPE_BOOL)
                    setAtom(e, fld->name(), refl->GetBool(*pbmsg_, fld));
            }
        }
    }

    std::string fmt_;
    core::FlatBuffersSchema fbs_;
    const reflection::Object* fbRoot_ = nullptr;
    std::unique_ptr<serializer::ProtobufSerializer> pb_;
    std::unique_ptr<google::protobuf::Message> pbmsg_;
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

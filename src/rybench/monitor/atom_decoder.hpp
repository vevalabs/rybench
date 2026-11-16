#pragma once
// Event + AtomDecoder — decode a serialized message (json|flatbuffers|protobuf) down
// to the four boolean atoms (p,q,r,s) our MTL benchmark properties are written over.
// Shared by every monitor backend (MonitorSink/Reelay, MonpolySink/MonPoly, and the
// offline harness) so decode is identical and implemented exactly once.

#include "flatbuffers_schema.hpp"
#include "protobuf_serializer.hpp"

#include <flatbuffers/reflection.h>
#include <rapidjson/document.h>
#include <google/protobuf/message.h>
#include <google/protobuf/descriptor.h>

#include <algorithm>
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

inline void setAtom(Event& e, const std::string& n, bool v) {
    if (n == "p") e.p = v;
    else if (n == "q") e.q = v;
    else if (n == "r") e.r = v;
    else if (n == "s") e.s = v;
}

class AtomDecoder {
public:
    // fmt: json|flatbuffers|protobuf; bfbs: schema path (unused for json).
    AtomDecoder(const std::string& fmt, const std::string& bfbs) : fmt_(fmt) {
        if (fmt_ == "flatbuffers") {
            fbs_.loadFromBfbs(bfbs);
            fbRoot_ = fbs_.reflectionSchema()->root_table();
        } else if (fmt_ == "protobuf") {
            pb_ = std::make_unique<serializer::ProtobufSerializer>(bfbs);
            pbmsg_.reset(pb_->newRootInstanceForTest());
        }
    }

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

private:
    std::string fmt_;
    core::FlatBuffersSchema fbs_;
    const reflection::Object* fbRoot_ = nullptr;
    std::unique_ptr<serializer::ProtobufSerializer> pb_;
    std::unique_ptr<google::protobuf::Message> pbmsg_;
};

// Latency sample summary (ns), shared by every monitor backend.
struct LatStats { uint64_t p50 = 0, p90 = 0, p99 = 0, max = 0; double mean = 0; size_t n = 0; };

inline LatStats computeStats(std::vector<uint32_t>& v) {
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

// Common surface every monitor backend (Reelay/MonitorSink, MonPoly/MonpolySink, ...)
// implements, so transport consumers can hold one without knowing which backend is
// behind it. The small vtable indirection is negligible next to decode + monitor cost.
class IMonitorSink {
public:
    virtual ~IMonitorSink() = default;

    virtual void feed(const uint8_t* payload, uint32_t size) = 0;
    virtual void feedTimed(const uint8_t* payload, uint32_t size) = 0;
    virtual void feedBreakdown(const uint8_t* payload, uint32_t size, uint64_t sendNs) = 0;

    // Must be called once, after the last feed*() and before reading count()/
    // trueRatio()/*Stats(): signals "no more input" and blocks until every fed
    // event has a verdict. A no-op for a backend that resolves synchronously inside
    // feed() itself (MonitorSink/Reelay); required for one that can only resolve an
    // event after seeing future events past it (MonpolySink with a future-time
    // formula), where feed() returns before that event's verdict exists yet.
    virtual void finish() = 0;

    virtual uint64_t count() const = 0;
    virtual double trueRatio() const = 0;

    virtual LatStats latencyStats() = 0;   // publish->verdict (end-to-end)
    virtual LatStats procStats()    = 0;   // receive->verdict (monitor-only)
    virtual LatStats transpStats()  = 0;   // send->incoming (transport)
    virtual LatStats parseStats()   = 0;   // decode/encode
    virtual LatStats monStats()     = 0;   // monitor update/round-trip
};

}  // namespace rybench::monitor

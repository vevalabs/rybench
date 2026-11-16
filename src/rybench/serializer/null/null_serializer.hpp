#pragma once

#include <rybench/serializer/serializer.hpp>
#include "../flatbuffers/flatbuffers_schema.hpp"
#include <string>
#include <string_view>
#include <vector>

namespace rybench::serializer {

// Generation backend that performs NO wire encoding. Every generated value is
// appended as raw bytes into a reusable buffer instead of being encoded into a
// wire format. It is used to measure pure generation throughput (obligation
// processing + value sampling) in isolation from serialization cost.
//
// The buffer is observable — it is returned inside the Message and consumed
// downstream — so neither the sampled values nor the generation loop can be
// optimized away. This keeps the measured numbers honest.
class NullSerializer : public Serializer {
public:
    explicit NullSerializer(const std::string& bfbsPath = "") {
        if (!bfbsPath.empty()) {
            try { fbsSchema_.loadFromBfbs(bfbsPath); } catch (...) {}
        }
        sink_.reserve(256);
    }

    std::string name() const override { return "null"; }
    core::MessageFormat format() const override { return core::MessageFormat::Unknown; }

    std::span<const uint8_t> serialize(const core::Message& msg) override {
        return msg.bytes();  // pass-through (usable as an output serializer too)
    }

    void loadSchema(const core::SchemaDefinition&) override {}
    const core::SchemaDefinition* loadedSchema() const override {
        return fbsSchema_.isLoaded() ? &fbsSchema_ : nullptr;
    }

    void initMessage() override { sink_.clear(); }

    void addBool  (const core::SchemaField&, bool v) override     { append(&v, sizeof v); }
    void addInt   (const core::SchemaField&, int64_t v) override  { append(&v, sizeof v); }
    void addUInt  (const core::SchemaField&, uint64_t v) override { append(&v, sizeof v); }
    void addFloat (const core::SchemaField&, float v) override    { append(&v, sizeof v); }
    void addDouble(const core::SchemaField&, double v) override   { append(&v, sizeof v); }
    void addString(const core::SchemaField&, std::string_view v) override {
        append(v.data(), v.size());
    }

    void beginTable (const core::SchemaField&) override {}
    void endTable   () override {}
    void beginVector(const core::SchemaField&, size_t) override {}
    void endVector  () override {}

    core::Message finalizeMessage() override {
        if (sink_.empty()) sink_.push_back(0);  // keep non-empty so the engine doesn't skip it
        return core::Message(sink_, core::MessageFormat::Unknown);  // copy out; sink_ keeps its capacity
    }

private:
    void append(const void* p, std::size_t n) {
        const auto* b = static_cast<const uint8_t*>(p);
        sink_.insert(sink_.end(), b, b + n);
    }

    core::FlatBuffersSchema fbsSchema_;
    std::vector<uint8_t>    sink_;
};

} // namespace rybench::serializer

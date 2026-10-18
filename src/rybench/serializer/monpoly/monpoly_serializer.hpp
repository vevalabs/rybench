#pragma once

#include <rybench/serializer/serializer.hpp>
#include <string>

#ifdef RYBENCH_ENABLE_FLATBUFFERS
#include "../flatbuffers/flatbuffers_schema.hpp"
#endif

namespace rybench::serializer {

// Encodes a message's boolean fields as MonPoly log facts: one "<name>()\n" line per
// field that is true, nothing for fields that are false -- MonPoly facts are
// presence-only, so a 0-ary predicate not holding at a timepoint is just absent, not
// written as false. No "@<ts>" header here: a timepoint is a property of the whole
// event stream (assigned by receive order), not of one message, so that belongs to
// the online monitor sink (MonpolySink) that reads these bytes off the transport.
class MonpolySerializer : public Serializer {
public:
    explicit MonpolySerializer(const std::string& bfbsPath = "");

    std::string name() const override;
    core::MessageFormat format() const override;
    std::span<const uint8_t> serialize(const core::Message& msg) override;

    void loadSchema(const core::SchemaDefinition& schema) override;
    const core::SchemaDefinition* loadedSchema() const override {
#ifdef RYBENCH_ENABLE_FLATBUFFERS
        return fbsSchema_.isLoaded() ? &fbsSchema_ : nullptr;
#else
        return nullptr;
#endif
    }
    void initMessage() override;
    void addBool  (const core::SchemaField& f, bool val) override;
    void addInt   (const core::SchemaField& f, int64_t val) override;
    void addUInt  (const core::SchemaField& f, uint64_t val) override;
    void addFloat (const core::SchemaField& f, float val) override;
    void addDouble(const core::SchemaField& f, double val) override;
    void addString(const core::SchemaField& f, std::string_view val) override;
    void beginTable (const core::SchemaField& f) override;
    void endTable   () override;
    void beginVector(const core::SchemaField& f, size_t count) override;
    void endVector  () override;
    core::Message finalizeMessage() override;

private:
    std::vector<uint8_t> buffer_;
#ifdef RYBENCH_ENABLE_FLATBUFFERS
    core::FlatBuffersSchema fbsSchema_;
#endif
};

} // namespace rybench::serializer

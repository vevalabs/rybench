#pragma once

#include <rybench/serializer/serializer.hpp>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <string>

#ifdef RYBENCH_ENABLE_FLATBUFFERS
#include "../flatbuffers/flatbuffers_schema.hpp"
#endif

namespace rybench::serializer {

class JsonSerializer : public Serializer {
public:
    explicit JsonSerializer(const std::string& bfbsPath = "");

    std::string name() const override;
    core::MessageFormat format() const override;
    std::span<const uint8_t> serialize(const core::Message& msg) override;

    void loadSchema(const core::SchemaDefinition& schema) override;
    const core::SchemaDefinition* loadedSchema() const override {
        return fbsSchema_.isLoaded() ? &fbsSchema_ : nullptr;
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
    void flatbufferToJson(rapidjson::Writer<rapidjson::StringBuffer>& writer, const core::Message& msg);
    void tableToJson(rapidjson::Writer<rapidjson::StringBuffer>& writer, const reflection::Object* obj, const flatbuffers::Table* table);
    void valueToJson(rapidjson::Writer<rapidjson::StringBuffer>& writer, const reflection::Field* field, const flatbuffers::Table* table);
#endif
};

} // namespace rybench::serializer

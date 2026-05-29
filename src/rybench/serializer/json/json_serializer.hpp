#pragma once

#include <rybench/serializer/serializer.hpp>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <string>

#ifdef RYBENCH_ENABLE_FLATBUFFERS
#include <rybench/core/flatbuffers_schema.hpp>
#endif

namespace rybench::serializer {

class JsonSerializer : public Serializer {
public:
    explicit JsonSerializer(const std::string& bfbsPath = "");

    std::string name() const override;
    core::MessageFormat format() const override;
    std::span<const uint8_t> serialize(const core::Message& msg) override;

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

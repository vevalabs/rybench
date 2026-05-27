#pragma once

#include <rybench/serializer/serializer.hpp>
#include <rybench/common/flatbuffers_schema.hpp>
#include <rapidjson/writer.h>
#include <rapidjson/stringbuffer.h>
#include <string>

namespace rybench::serializer {

class JsonSerializer : public Serializer {
public:
    explicit JsonSerializer(const std::string& bfbsPath = "");

    std::span<const uint8_t> serialize(const common::Message& msg) override;
    std::string name() const override;

private:
    common::FlatBuffersSchema fbsSchema_;
    std::vector<uint8_t> buffer_;

    // FlatBuffer to JSON conversion helpers using RapidJSON Writer
    void flatbufferToJson(rapidjson::Writer<rapidjson::StringBuffer>& writer, const common::Message& msg);
    void tableToJson(rapidjson::Writer<rapidjson::StringBuffer>& writer, const reflection::Object* obj, const flatbuffers::Table* table);
    void valueToJson(rapidjson::Writer<rapidjson::StringBuffer>& writer, const reflection::Field* field, const flatbuffers::Table* table);
};

} // namespace rybench::serializer

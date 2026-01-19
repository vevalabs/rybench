#pragma once

#include "serializer.hpp"
#include "flatbuffers_schema.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace rybench {
namespace serializer {

class JsonSerializer : public ISerializer {
public:
    explicit JsonSerializer(const std::string& bfbsPath = "");

    std::vector<uint8_t> serialize(const common::Message& msg) override;
    std::string name() const override;

private:
    common::FlatBuffersSchema fbsSchema_;

    // FlatBuffer to JSON conversion helpers
    nlohmann::json flatbufferToJson(const common::Message& msg);
    nlohmann::json tableToJson(const reflection::Object* obj, const flatbuffers::Table* table);
    nlohmann::json valueToJson(const reflection::Field* field, const flatbuffers::Table* table);
};

} // namespace serializer
} // namespace rybench

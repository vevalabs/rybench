#pragma once

#include "serializer.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace serializer {

class JsonSerializer : public ISerializer {
public:
    explicit JsonSerializer(const std::string& schemaPath = "");

    std::vector<uint8_t> serialize(const common::DataValue& value) override;
    common::DataValue deserialize(const std::vector<uint8_t>& data) override;
    common::Schema getSchema() const override;
    std::string name() const override;

private:
    common::Schema schema_;

    nlohmann::json toJson(const common::DataValue& value);
    common::DataValue fromJson(const nlohmann::json& j);
};

} // namespace serializer

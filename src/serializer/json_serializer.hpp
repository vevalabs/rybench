#pragma once

#include "serializer.hpp"
#include <nlohmann/json.hpp>

namespace serializer {

class JsonSerializer : public ISerializer {
public:
    explicit JsonSerializer(const std::string& schemaPath = "") {
        if (!schemaPath.empty()) {
            schema_.loadFromFile(schemaPath);
        }
    }

    std::vector<uint8_t> serialize(const common::DataValue& value) override {
        nlohmann::json j = toJson(value);
        std::string str = j.dump();
        return std::vector<uint8_t>(str.begin(), str.end());
    }

    common::DataValue deserialize(const std::vector<uint8_t>& data) override {
        std::string str(data.begin(), data.end());
        nlohmann::json j = nlohmann::json::parse(str);
        return fromJson(j);
    }

    common::Schema getSchema() const override {
        return schema_;
    }

    std::string name() const override { return "json"; }

private:
    common::Schema schema_;

private:
    nlohmann::json toJson(const common::DataValue& value) {
        if (value.isNull()) {
            return nullptr;
        }
        if (value.isBool()) {
            return value.asBool();
        }
        if (value.isInt()) {
            return value.asInt();
        }
        if (value.isDouble()) {
            return value.asDouble();
        }
        if (value.isString()) {
            return value.asString();
        }
        if (value.isArray()) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& item : value.asArray()) {
                arr.push_back(toJson(item));
            }
            return arr;
        }
        if (value.isObject()) {
            nlohmann::json obj = nlohmann::json::object();
            for (const auto& [key, val] : value.asObject()) {
                obj[key] = toJson(val);
            }
            return obj;
        }
        return nullptr;
    }

    common::DataValue fromJson(const nlohmann::json& j) {
        if (j.is_null()) {
            return common::DataValue(nullptr);
        }
        if (j.is_boolean()) {
            return common::DataValue(j.get<bool>());
        }
        if (j.is_number_integer()) {
            return common::DataValue(j.get<int64_t>());
        }
        if (j.is_number_float()) {
            return common::DataValue(j.get<double>());
        }
        if (j.is_string()) {
            return common::DataValue(j.get<std::string>());
        }
        if (j.is_array()) {
            common::DataArray arr;
            for (const auto& item : j) {
                arr.push_back(fromJson(item));
            }
            return common::DataValue(std::move(arr));
        }
        if (j.is_object()) {
            common::DataObject obj;
            for (auto it = j.begin(); it != j.end(); ++it) {
                obj[it.key()] = fromJson(it.value());
            }
            return common::DataValue(std::move(obj));
        }
        return common::DataValue(nullptr);
    }
};

} // namespace serializer

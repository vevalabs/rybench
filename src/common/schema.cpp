#include "schema.hpp"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace common {

void Schema::loadFromFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open schema file: " + filepath);
    }
    nlohmann::json j;
    file >> j;
    parseSchema(j);
}

void Schema::loadFromJson(const nlohmann::json& j) {
    parseSchema(j);
}

void Schema::parseSchema(const nlohmann::json& j) {
    if (j.contains("name")) {
        name_ = j["name"].get<std::string>();
    }

    if (j.contains("fields")) {
        for (const auto& field : j["fields"]) {
            fields_.push_back(parseField(field));
        }
    }
}

SchemaField Schema::parseField(const nlohmann::json& j) {
    SchemaField field;
    field.name = j["name"].get<std::string>();
    field.type = parseType(j["type"].get<std::string>());

    if (j.contains("min")) {
        field.constraints.min = j["min"].get<double>();
    }
    if (j.contains("max")) {
        field.constraints.max = j["max"].get<double>();
    }
    if (j.contains("minLength")) {
        field.constraints.minLength = j["minLength"].get<size_t>();
    }
    if (j.contains("maxLength")) {
        field.constraints.maxLength = j["maxLength"].get<size_t>();
    }
    if (j.contains("arraySize")) {
        field.constraints.arraySize = j["arraySize"].get<size_t>();
    }

    if (j.contains("fields")) {
        for (const auto& child : j["fields"]) {
            field.children.push_back(parseField(child));
        }
    }

    if (j.contains("items")) {
        field.children.push_back(parseField(j["items"]));
    }

    return field;
}

FieldType Schema::parseType(const std::string& type) {
    if (type == "int" || type == "integer") return FieldType::Int;
    if (type == "double" || type == "float" || type == "number") return FieldType::Double;
    if (type == "string") return FieldType::String;
    if (type == "bool" || type == "boolean") return FieldType::Bool;
    if (type == "array") return FieldType::Array;
    if (type == "object") return FieldType::Object;
    throw std::runtime_error("Unknown field type: " + type);
}

} // namespace common

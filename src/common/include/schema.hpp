#pragma once

#include <string>
#include <vector>
#include <optional>
#include <nlohmann/json_fwd.hpp>

namespace rybench {
namespace common {

enum class FieldType {
    Int,
    Double,
    String,
    Bool,
    Array,
    Object
};

struct FieldConstraints {
    std::optional<double> min;
    std::optional<double> max;
    std::optional<size_t> minLength;
    std::optional<size_t> maxLength;
    std::optional<size_t> arraySize;
};

struct SchemaField {
    std::string name;
    FieldType type;
    FieldConstraints constraints;
    std::vector<SchemaField> children;
};

class Schema {
public:
    Schema() = default;

    void loadFromFile(const std::string& filepath);
    void loadFromJson(const nlohmann::json& j);

    const std::vector<SchemaField>& fields() const { return fields_; }
    const std::string& name() const { return name_; }

private:
    std::string name_;
    std::vector<SchemaField> fields_;

    void parseSchema(const nlohmann::json& j);
    SchemaField parseField(const nlohmann::json& j);
    static FieldType parseType(const std::string& type);
};

} // namespace common
} // namespace rybench

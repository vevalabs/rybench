#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rybench::core {

enum class FieldType {
    Bool,
    Byte, UByte,
    Short, UShort,
    Int, UInt,
    Long, ULong,
    Float, Double,
    String,
    Vector,
    Struct,
    Table,
    Enum,
    Unknown
};

struct FieldConstraints {
    std::optional<double> min;
    std::optional<double> max;
    std::optional<size_t> minLength;
    std::optional<size_t> maxLength;
    std::optional<std::string> pattern;
    std::optional<size_t> minItems;
    std::optional<size_t> maxItems;
};

struct SchemaField {
    std::string      name;
    FieldType        type       = FieldType::Unknown;
    FieldConstraints constraints;
    std::vector<SchemaField> children;

    uint16_t fieldId   = 0;   // vtable offset (FlatBuffers); ignored by other formats
    int32_t  typeIndex = -1;  // format-specific type index

    bool isRequired = false;
    bool isVector   = false;

    std::vector<std::string> enumValues;
    std::vector<int64_t>     enumNumericValues;

    int64_t defaultInt    = 0;
    double  defaultDouble = 0.0;
};

class SchemaDefinition {
public:
    SchemaDefinition() = default;
    virtual ~SchemaDefinition() = default;

    const std::vector<SchemaField>& fields() const { return fields_; }
    const std::string& rootName() const { return rootName_; }
    virtual bool isLoaded() const { return !rootName_.empty(); }

    // Format-specific metadata (e.g. FlatBuffers table attributes)
    virtual std::string attribute(const std::string& /*key*/) const { return {}; }

protected:
    std::vector<SchemaField> fields_;
    std::string              rootName_;
};

} // namespace rybench::core

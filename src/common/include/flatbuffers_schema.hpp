#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <flatbuffers/reflection.h>

namespace common {

enum class FbsFieldType {
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

struct FbsFieldConstraints {
    // Numeric constraints
    std::optional<double> min;
    std::optional<double> max;

    // String constraints
    std::optional<size_t> minLength;
    std::optional<size_t> maxLength;
    std::optional<std::string> pattern;

    // Array/Vector constraints
    std::optional<size_t> minItems;
    std::optional<size_t> maxItems;
};

struct FbsSchemaField {
    std::string name;
    FbsFieldType type;
    FbsFieldConstraints constraints;
    std::vector<FbsSchemaField> children;  // For tables, structs, vectors

    // FlatBuffers reflection indices
    uint16_t fieldId = 0;           // Field ID in vtable
    int32_t typeIndex = -1;         // Index into schema objects array
    reflection::BaseType baseType;  // Raw FlatBuffers base type

    bool isRequired = false;
    bool isVector = false;

    // For enum fields
    std::vector<std::string> enumValues;
};

class FlatBuffersSchema {
public:
    FlatBuffersSchema() = default;

    // Load from compiled .bfbs file
    void loadFromBfbs(const std::string& filepath);
    void loadFromBfbs(const std::vector<uint8_t>& bfbsData);

    // Access parsed schema
    const std::vector<FbsSchemaField>& fields() const { return fields_; }
    const std::string& rootTableName() const { return rootTableName_; }

    // Access underlying reflection schema
    const reflection::Schema* reflectionSchema() const { return reflSchema_; }
    const reflection::Object* rootTable() const;

    // Binary schema data (for lifetime management)
    const std::vector<uint8_t>& bfbsData() const { return bfbsData_; }

    bool isLoaded() const { return reflSchema_ != nullptr; }

private:
    std::vector<uint8_t> bfbsData_;
    const reflection::Schema* reflSchema_ = nullptr;
    std::string rootTableName_;
    std::vector<FbsSchemaField> fields_;

    void parseReflectionSchema();
    FbsSchemaField parseField(const reflection::Field* field);
    FbsSchemaField parseObject(const reflection::Object* obj);
    void parseAttributes(const reflection::Field* field, FbsFieldConstraints& constraints);
    static FbsFieldType baseTypeToFieldType(reflection::BaseType baseType);
};

} // namespace common

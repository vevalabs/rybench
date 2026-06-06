#pragma once

#include <rybench/core/schema.hpp>
#include <flatbuffers/reflection.h>
#include <string>
#include <vector>

namespace rybench::core {

// Backward-compat aliases
using FbsFieldType        = FieldType;
using FbsFieldConstraints = FieldConstraints;
using FbsSchemaField      = SchemaField;

class FlatBuffersSchema : public SchemaDefinition {
public:
    FlatBuffersSchema() = default;

    void loadFromBfbs(const std::string& filepath);
    void loadFromBfbs(const std::vector<uint8_t>& bfbsData);

    bool isLoaded() const override { return reflSchema_ != nullptr; }

    // Looks up a table-level attribute from the root table (e.g. "mtl")
    std::string attribute(const std::string& key) const override;

    // FlatBuffers-specific — used only by FlatbuffersSerializer internals
    const reflection::Schema* reflectionSchema() const { return reflSchema_; }
    const reflection::Object* rootTable() const;
    const std::vector<uint8_t>& bfbsData() const { return bfbsData_; }

private:
    std::vector<uint8_t>       bfbsData_;
    const reflection::Schema*  reflSchema_ = nullptr;

    void parseReflectionSchema();
    SchemaField parseField(const reflection::Field* field);
    SchemaField parseObject(const reflection::Object* obj);
    void parseAttributes(const reflection::Field* field, FieldConstraints& constraints);
    static FieldType baseTypeToFieldType(reflection::BaseType baseType);
};

} // namespace rybench::core

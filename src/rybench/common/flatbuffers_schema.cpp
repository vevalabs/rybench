#include "include/flatbuffers_schema.hpp"
#include <fstream>
#include <stdexcept>
#include <algorithm>

namespace rybench {
namespace common {

void FlatBuffersSchema::loadFromBfbs(const std::string& filepath) {
    std::ifstream file(filepath, std::ios::binary | std::ios::ate);
    if (!file) {
        throw std::runtime_error("Failed to open bfbs file: " + filepath);
    }

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    bfbsData_.resize(static_cast<size_t>(size));
    if (!file.read(reinterpret_cast<char*>(bfbsData_.data()), size)) {
        throw std::runtime_error("Failed to read bfbs file: " + filepath);
    }

    loadFromBfbs(bfbsData_);
}

void FlatBuffersSchema::loadFromBfbs(const std::vector<uint8_t>& bfbsData) {
    if (&bfbsData != &bfbsData_) {
        bfbsData_ = bfbsData;
    }

    reflSchema_ = reflection::GetSchema(bfbsData_.data());
    if (!reflSchema_) {
        throw std::runtime_error("Invalid bfbs data: failed to parse reflection schema");
    }

    parseReflectionSchema();
}

const reflection::Object* FlatBuffersSchema::rootTable() const {
    if (!reflSchema_) {
        return nullptr;
    }
    return reflSchema_->root_table();
}

void FlatBuffersSchema::parseReflectionSchema() {
    if (!reflSchema_ || !reflSchema_->root_table()) {
        return;
    }

    auto* rootObj = reflSchema_->root_table();
    rootTableName_ = rootObj->name()->str();

    // Parse fields of the root table
    if (rootObj->fields()) {
        for (const auto* field : *rootObj->fields()) {
            fields_.push_back(parseField(field));
        }
    }
}

FbsSchemaField FlatBuffersSchema::parseField(const reflection::Field* field) {
    FbsSchemaField sf;
    sf.name = field->name()->str();
    sf.fieldId = field->id();
    sf.isRequired = field->required();

    auto baseType = field->type()->base_type();
    sf.isVector = (baseType == reflection::BaseType::Vector);

    // Determine field type
    if (sf.isVector) {
        sf.type = FbsFieldType::Vector;
        sf.typeIndex = field->type()->index();

        // Parse element type for vectors
        auto elementType = field->type()->element();
        if (elementType == reflection::BaseType::Obj) {
            // Vector of tables/structs
            if (field->type()->index() >= 0) {
                auto* elementObj = reflSchema_->objects()->Get(field->type()->index());
                if (elementObj) {
                    sf.children.push_back(parseObject(elementObj));
                }
            }
        } else {
            // Vector of scalars
            FbsSchemaField elementField;
            elementField.name = "element";
            elementField.type = baseTypeToFieldType(elementType);
            sf.children.push_back(elementField);
        }
    } else if (baseType == reflection::BaseType::Obj) {
        sf.typeIndex = field->type()->index();
        if (sf.typeIndex >= 0) {
            auto* obj = reflSchema_->objects()->Get(sf.typeIndex);
            if (obj) {
                sf.type = obj->is_struct() ? FbsFieldType::Struct : FbsFieldType::Table;
                // Recursively parse nested object fields
                sf.children = parseObject(obj).children;
            }
        }
    } else {
        sf.type = baseTypeToFieldType(baseType);
    }

    // Parse constraints from attributes
    parseAttributes(field, sf.constraints);

    return sf;
}

FbsSchemaField FlatBuffersSchema::parseObject(const reflection::Object* obj) {
    FbsSchemaField sf;
    sf.name = obj->name()->str();
    sf.type = obj->is_struct() ? FbsFieldType::Struct : FbsFieldType::Table;

    if (obj->fields()) {
        for (const auto* field : *obj->fields()) {
            sf.children.push_back(parseField(field));
        }
    }

    return sf;
}

void FlatBuffersSchema::parseAttributes(const reflection::Field* field, FbsFieldConstraints& constraints) {
    if (!field->attributes()) {
        return;
    }

    for (const auto* attr : *field->attributes()) {
        if (!attr->key() || !attr->value()) {
            continue;
        }

        std::string key = attr->key()->str();
        std::string value = attr->value()->str();

        try {
            if (key == "min") {
                constraints.min = std::stod(value);
            } else if (key == "max") {
                constraints.max = std::stod(value);
            } else if (key == "min_length") {
                constraints.minLength = std::stoull(value);
            } else if (key == "max_length") {
                constraints.maxLength = std::stoull(value);
            } else if (key == "pattern") {
                constraints.pattern = value;
            } else if (key == "min_items") {
                constraints.minItems = std::stoull(value);
            } else if (key == "max_items") {
                constraints.maxItems = std::stoull(value);
            }
        } catch (const std::exception&) {
            // Ignore invalid attribute values
        }
    }
}

FbsFieldType FlatBuffersSchema::baseTypeToFieldType(reflection::BaseType baseType) {
    switch (baseType) {
        case reflection::BaseType::Bool:
            return FbsFieldType::Bool;
        case reflection::BaseType::Byte:
            return FbsFieldType::Byte;
        case reflection::BaseType::UByte:
            return FbsFieldType::UByte;
        case reflection::BaseType::Short:
            return FbsFieldType::Short;
        case reflection::BaseType::UShort:
            return FbsFieldType::UShort;
        case reflection::BaseType::Int:
            return FbsFieldType::Int;
        case reflection::BaseType::UInt:
            return FbsFieldType::UInt;
        case reflection::BaseType::Long:
            return FbsFieldType::Long;
        case reflection::BaseType::ULong:
            return FbsFieldType::ULong;
        case reflection::BaseType::Float:
            return FbsFieldType::Float;
        case reflection::BaseType::Double:
            return FbsFieldType::Double;
        case reflection::BaseType::String:
            return FbsFieldType::String;
        case reflection::BaseType::Vector:
            return FbsFieldType::Vector;
        case reflection::BaseType::Obj:
            return FbsFieldType::Table;
        default:
            return FbsFieldType::Unknown;
    }
}

} // namespace common
} // namespace rybench

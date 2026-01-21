#include "include/reflection_builder.hpp"
#include <chrono>
#include <algorithm>
#include <cstring>

namespace rybench {
namespace generator {

ReflectionBuilder::ReflectionBuilder(const common::FlatBuffersSchema& schema)
    : schema_(schema) {}

common::Message ReflectionBuilder::build(std::mt19937_64& rng) {
    if (!schema_.isLoaded()) {
        return common::Message();
    }

    flatbuffers::FlatBufferBuilder builder(1024);

    auto* rootTable = schema_.rootTable();
    if (!rootTable) {
        return common::Message();
    }

    auto rootOffset = buildTable(builder, rootTable, schema_.fields(), rng, "");

    builder.Finish(flatbuffers::Offset<flatbuffers::Table>(rootOffset.o));

    auto* buf = builder.GetBufferPointer();
    auto size = builder.GetSize();

    std::vector<uint8_t> buffer(buf, buf + size);
    return common::Message(std::move(buffer), schema_.reflectionSchema());
}

void ReflectionBuilder::setIntGenerator(const std::string& fieldPath, ScalarGenerator gen) {
    intGenerators_[fieldPath] = std::move(gen);
}

void ReflectionBuilder::setDoubleGenerator(const std::string& fieldPath, DoubleGenerator gen) {
    doubleGenerators_[fieldPath] = std::move(gen);
}

void ReflectionBuilder::setStringGenerator(const std::string& fieldPath, StringGenerator gen) {
    stringGenerators_[fieldPath] = std::move(gen);
}

void ReflectionBuilder::setCounter(const std::string& fieldPath, int64_t start) {
    counters_[fieldPath] = start;
    intGenerators_[fieldPath] = [this, fieldPath](std::mt19937_64&) {
        return counters_[fieldPath]++;
    };
}

void ReflectionBuilder::setTimestamp(const std::string& fieldPath) {
    intGenerators_[fieldPath] = [](std::mt19937_64&) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    };
}

void ReflectionBuilder::setFixedValue(const std::string& fieldPath, int64_t value) {
    intGenerators_[fieldPath] = [value](std::mt19937_64&) { return value; };
}

void ReflectionBuilder::setFixedValue(const std::string& fieldPath, double value) {
    doubleGenerators_[fieldPath] = [value](std::mt19937_64&) { return value; };
}

void ReflectionBuilder::setFixedValue(const std::string& fieldPath, const std::string& value) {
    stringGenerators_[fieldPath] = [value](std::mt19937_64&) { return value; };
}

flatbuffers::Offset<void> ReflectionBuilder::buildTable(
    flatbuffers::FlatBufferBuilder& builder,
    const reflection::Object* table,
    const std::vector<common::FbsSchemaField>& fields,
    std::mt19937_64& rng,
    const std::string& path) {

    if (!table->fields()) {
        return flatbuffers::Offset<void>(0);
    }

    // Collect all offsets first (strings, vectors, nested tables must be created before StartTable)
    std::vector<std::pair<uint16_t, flatbuffers::Offset<void>>> offsets;

    for (const auto& sf : fields) {
        auto* field = table->fields()->LookupByKey(sf.name.c_str());
        if (!field) continue;

        std::string fPath = fieldPath(path, sf.name);
        auto baseType = field->type()->base_type();

        if (baseType == reflection::BaseType::String) {
            auto strOffset = buildString(builder, sf.constraints, rng, fPath);
            offsets.emplace_back(field->offset(), strOffset.Union());
        } else if (baseType == reflection::BaseType::Vector) {
            auto vecOffset = buildVector(builder, field, sf, rng, fPath);
            offsets.emplace_back(field->offset(), vecOffset);
        } else if (baseType == reflection::BaseType::Obj) {
            auto* nestedObj = schema_.reflectionSchema()->objects()->Get(field->type()->index());
            if (nestedObj && !nestedObj->is_struct()) {
                auto nestedOffset = buildTable(builder, nestedObj, sf.children, rng, fPath);
                offsets.emplace_back(field->offset(), nestedOffset);
            }
        }
    }

    // Start table construction
    auto start = builder.StartTable();

    // Add all pre-built offsets
    for (const auto& [id, offset] : offsets) {
        builder.AddOffset(id, offset);
    }

    // Add scalar fields
    for (const auto& sf : fields) {
        auto* field = table->fields()->LookupByKey(sf.name.c_str());
        if (!field) continue;

        std::string fPath = fieldPath(path, sf.name);
        auto baseType = field->type()->base_type();

        // Check if this field is an enum
        int32_t enumIndex = field->type()->index();
        bool isEnum = (enumIndex >= 0) &&
                      (baseType == reflection::BaseType::Byte ||
                       baseType == reflection::BaseType::UByte ||
                       baseType == reflection::BaseType::Short ||
                       baseType == reflection::BaseType::UShort ||
                       baseType == reflection::BaseType::Int ||
                       baseType == reflection::BaseType::UInt);

        if (isEnum && schema_.reflectionSchema()->enums()) {
            auto* enumDef = schema_.reflectionSchema()->enums()->Get(enumIndex);
            if (enumDef && enumDef->values() && enumDef->values()->size() > 0) {
                std::uniform_int_distribution<size_t> dist(0, enumDef->values()->size() - 1);
                auto enumValue = enumDef->values()->Get(dist(rng))->value();

                switch (baseType) {
                    case reflection::BaseType::Byte:
                        builder.AddElement<int8_t>(field->offset(),
                            static_cast<int8_t>(enumValue),
                            static_cast<int8_t>(field->default_integer()));
                        break;
                    case reflection::BaseType::UByte:
                        builder.AddElement<uint8_t>(field->offset(),
                            static_cast<uint8_t>(enumValue),
                            static_cast<uint8_t>(field->default_integer()));
                        break;
                    case reflection::BaseType::Short:
                        builder.AddElement<int16_t>(field->offset(),
                            static_cast<int16_t>(enumValue),
                            static_cast<int16_t>(field->default_integer()));
                        break;
                    case reflection::BaseType::UShort:
                        builder.AddElement<uint16_t>(field->offset(),
                            static_cast<uint16_t>(enumValue),
                            static_cast<uint16_t>(field->default_integer()));
                        break;
                    case reflection::BaseType::Int:
                        builder.AddElement<int32_t>(field->offset(),
                            static_cast<int32_t>(enumValue),
                            static_cast<int32_t>(field->default_integer()));
                        break;
                    case reflection::BaseType::UInt:
                        builder.AddElement<uint32_t>(field->offset(),
                            static_cast<uint32_t>(enumValue),
                            static_cast<uint32_t>(field->default_integer()));
                        break;
                    default:
                        break;
                }
                continue;  // Skip the regular switch below
            }
        }

        switch (baseType) {
            case reflection::BaseType::Bool:
                builder.AddElement<uint8_t>(field->offset(), generateBool(rng) ? 1 : 0,
                    static_cast<uint8_t>(field->default_integer()));
                break;
            case reflection::BaseType::Byte:
                builder.AddElement<int8_t>(field->offset(),
                    static_cast<int8_t>(generateInt(sf.constraints, rng, fPath, false)),
                    static_cast<int8_t>(field->default_integer()));
                break;
            case reflection::BaseType::UByte:
                builder.AddElement<uint8_t>(field->offset(),
                    static_cast<uint8_t>(generateInt(sf.constraints, rng, fPath, true)),
                    static_cast<uint8_t>(field->default_integer()));
                break;
            case reflection::BaseType::Short:
                builder.AddElement<int16_t>(field->offset(),
                    static_cast<int16_t>(generateInt(sf.constraints, rng, fPath, false)),
                    static_cast<int16_t>(field->default_integer()));
                break;
            case reflection::BaseType::UShort:
                builder.AddElement<uint16_t>(field->offset(),
                    static_cast<uint16_t>(generateInt(sf.constraints, rng, fPath, true)),
                    static_cast<uint16_t>(field->default_integer()));
                break;
            case reflection::BaseType::Int:
                builder.AddElement<int32_t>(field->offset(),
                    static_cast<int32_t>(generateInt(sf.constraints, rng, fPath, false)),
                    static_cast<int32_t>(field->default_integer()));
                break;
            case reflection::BaseType::UInt:
                builder.AddElement<uint32_t>(field->offset(),
                    static_cast<uint32_t>(generateInt(sf.constraints, rng, fPath, true)),
                    static_cast<uint32_t>(field->default_integer()));
                break;
            case reflection::BaseType::Long:
                builder.AddElement<int64_t>(field->offset(),
                    generateInt(sf.constraints, rng, fPath, false),
                    field->default_integer());
                break;
            case reflection::BaseType::ULong:
                builder.AddElement<uint64_t>(field->offset(),
                    static_cast<uint64_t>(generateInt(sf.constraints, rng, fPath, true)),
                    static_cast<uint64_t>(field->default_integer()));
                break;
            case reflection::BaseType::Float:
                builder.AddElement<float>(field->offset(),
                    static_cast<float>(generateDouble(sf.constraints, rng, fPath)),
                    static_cast<float>(field->default_real()));
                break;
            case reflection::BaseType::Double:
                builder.AddElement<double>(field->offset(),
                    generateDouble(sf.constraints, rng, fPath),
                    field->default_real());
                break;
            case reflection::BaseType::Obj:
                // Handle inline structs
                if (field->type()->index() >= 0) {
                    auto* obj = schema_.reflectionSchema()->objects()->Get(field->type()->index());
                    if (obj && obj->is_struct()) {
                        // Structs are inlined - build directly into vtable
                        auto structOffset = buildStruct(builder, obj, sf.children, rng, fPath);
                        // Note: Structs in FlatBuffers are stored inline, handling varies
                    }
                }
                break;
            default:
                break;
        }
    }

    return flatbuffers::Offset<void>(builder.EndTable(start));
}

flatbuffers::Offset<flatbuffers::String> ReflectionBuilder::buildString(
    flatbuffers::FlatBufferBuilder& builder,
    const common::FbsFieldConstraints& constraints,
    std::mt19937_64& rng,
    const std::string& path) {

    std::string str = generateString(constraints, rng, path);
    return builder.CreateString(str);
}

flatbuffers::Offset<void> ReflectionBuilder::buildVector(
    flatbuffers::FlatBufferBuilder& builder,
    const reflection::Field* field,
    const common::FbsSchemaField& schemaField,
    std::mt19937_64& rng,
    const std::string& path) {

    size_t minItems = schemaField.constraints.minItems.value_or(1);
    size_t maxItems = schemaField.constraints.maxItems.value_or(5);

    std::uniform_int_distribution<size_t> sizeDist(minItems, maxItems);
    size_t count = sizeDist(rng);

    auto elementType = field->type()->element();

    if (elementType == reflection::BaseType::Obj) {
        // Vector of tables/structs
        int32_t objIndex = field->type()->index();
        if (objIndex >= 0) {
            auto* elementObj = schema_.reflectionSchema()->objects()->Get(objIndex);
            if (elementObj && !elementObj->is_struct()) {
                // Vector of tables
                std::vector<flatbuffers::Offset<flatbuffers::Table>> tableOffsets;
                for (size_t i = 0; i < count; ++i) {
                    std::string elemPath = path + "[" + std::to_string(i) + "]";
                    auto& elementFields = schemaField.children.empty() ?
                        std::vector<common::FbsSchemaField>() : schemaField.children[0].children;
                    auto offset = buildTable(builder, elementObj, elementFields, rng, elemPath);
                    tableOffsets.push_back(flatbuffers::Offset<flatbuffers::Table>(offset.o));
                }
                return builder.CreateVector(tableOffsets).Union();
            }
        }
    } else if (elementType == reflection::BaseType::String) {
        // Vector of strings
        std::vector<flatbuffers::Offset<flatbuffers::String>> strOffsets;
        common::FbsFieldConstraints elemConstraints;
        if (!schemaField.children.empty()) {
            elemConstraints = schemaField.children[0].constraints;
        }
        for (size_t i = 0; i < count; ++i) {
            std::string elemPath = path + "[" + std::to_string(i) + "]";
            strOffsets.push_back(buildString(builder, elemConstraints, rng, elemPath));
        }
        return builder.CreateVector(strOffsets).Union();
    } else {
        // Vector of scalars
        common::FbsFieldConstraints elemConstraints;
        if (!schemaField.children.empty()) {
            elemConstraints = schemaField.children[0].constraints;
        }

        switch (elementType) {
            case reflection::BaseType::Int: {
                std::vector<int32_t> values;
                for (size_t i = 0; i < count; ++i) {
                    values.push_back(static_cast<int32_t>(
                        generateInt(elemConstraints, rng, path, false)));
                }
                return builder.CreateVector(values).Union();
            }
            case reflection::BaseType::UInt: {
                std::vector<uint32_t> values;
                for (size_t i = 0; i < count; ++i) {
                    values.push_back(static_cast<uint32_t>(
                        generateInt(elemConstraints, rng, path, true)));
                }
                return builder.CreateVector(values).Union();
            }
            case reflection::BaseType::Long: {
                std::vector<int64_t> values;
                for (size_t i = 0; i < count; ++i) {
                    values.push_back(generateInt(elemConstraints, rng, path, false));
                }
                return builder.CreateVector(values).Union();
            }
            case reflection::BaseType::ULong: {
                std::vector<uint64_t> values;
                for (size_t i = 0; i < count; ++i) {
                    values.push_back(static_cast<uint64_t>(
                        generateInt(elemConstraints, rng, path, true)));
                }
                return builder.CreateVector(values).Union();
            }
            case reflection::BaseType::Float: {
                std::vector<float> values;
                for (size_t i = 0; i < count; ++i) {
                    values.push_back(static_cast<float>(
                        generateDouble(elemConstraints, rng, path)));
                }
                return builder.CreateVector(values).Union();
            }
            case reflection::BaseType::Double: {
                std::vector<double> values;
                for (size_t i = 0; i < count; ++i) {
                    values.push_back(generateDouble(elemConstraints, rng, path));
                }
                return builder.CreateVector(values).Union();
            }
            case reflection::BaseType::Bool: {
                std::vector<uint8_t> values;
                for (size_t i = 0; i < count; ++i) {
                    values.push_back(generateBool(rng) ? 1 : 0);
                }
                return builder.CreateVector(values).Union();
            }
            default:
                break;
        }
    }

    return flatbuffers::Offset<void>(0);
}

flatbuffers::Offset<void> ReflectionBuilder::buildStruct(
    flatbuffers::FlatBufferBuilder& builder,
    const reflection::Object* structDef,
    const std::vector<common::FbsSchemaField>& fields,
    std::mt19937_64& rng,
    const std::string& path) {

    // Structs in FlatBuffers are stored inline with fixed layout
    // For now, we don't support inline structs via reflection building
    // The schema should use tables instead of structs for dynamic generation
    (void)builder;
    (void)structDef;
    (void)fields;
    (void)rng;
    (void)path;

    // Return empty offset - structs need special handling
    return flatbuffers::Offset<void>(0);
}

int64_t ReflectionBuilder::generateInt(
    const common::FbsFieldConstraints& c,
    std::mt19937_64& rng,
    const std::string& path,
    bool isUnsigned) {

    // Check for custom generator
    auto it = intGenerators_.find(path);
    if (it != intGenerators_.end()) {
        return it->second(rng);
    }

    int64_t minVal = static_cast<int64_t>(c.min.value_or(isUnsigned ? 0 : -1000));
    int64_t maxVal = static_cast<int64_t>(c.max.value_or(1000));

    if (isUnsigned && minVal < 0) minVal = 0;

    std::uniform_int_distribution<int64_t> dist(minVal, maxVal);
    return dist(rng);
}

double ReflectionBuilder::generateDouble(
    const common::FbsFieldConstraints& c,
    std::mt19937_64& rng,
    const std::string& path) {

    // Check for custom generator
    auto it = doubleGenerators_.find(path);
    if (it != doubleGenerators_.end()) {
        return it->second(rng);
    }

    double minVal = c.min.value_or(-100.0);
    double maxVal = c.max.value_or(100.0);

    std::uniform_real_distribution<double> dist(minVal, maxVal);
    return dist(rng);
}

bool ReflectionBuilder::generateBool(std::mt19937_64& rng) {
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(rng) == 1;
}

std::string ReflectionBuilder::generateString(
    const common::FbsFieldConstraints& c,
    std::mt19937_64& rng,
    const std::string& path) {

    // Check for custom generator
    auto it = stringGenerators_.find(path);
    if (it != stringGenerators_.end()) {
        return it->second(rng);
    }

    size_t minLen = c.minLength.value_or(3);
    size_t maxLen = c.maxLength.value_or(12);

    if (c.pattern) {
        return generatePatternString(*c.pattern, minLen, maxLen, rng);
    }

    std::uniform_int_distribution<size_t> lenDist(minLen, maxLen);
    size_t len = lenDist(rng);

    static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<size_t> charDist(0, sizeof(charset) - 2);

    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        result += charset[charDist(rng)];
    }
    return result;
}

std::string ReflectionBuilder::generatePatternString(
    const std::string& pattern,
    size_t minLen,
    size_t maxLen,
    std::mt19937_64& rng) {

    // Simple pattern support for common cases
    std::string charset;

    if (pattern == "^[A-Z]+$" || pattern.find("[A-Z]") != std::string::npos) {
        charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    } else if (pattern == "^[a-z]+$" || pattern.find("[a-z]") != std::string::npos) {
        charset = "abcdefghijklmnopqrstuvwxyz";
    } else if (pattern == "^[0-9]+$" || pattern.find("[0-9]") != std::string::npos) {
        charset = "0123456789";
    } else if (pattern.find("[A-Za-z]") != std::string::npos) {
        charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    } else {
        // Default to alphanumeric
        charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    }

    std::uniform_int_distribution<size_t> lenDist(minLen, maxLen);
    size_t len = lenDist(rng);

    std::uniform_int_distribution<size_t> charDist(0, charset.size() - 1);

    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        result += charset[charDist(rng)];
    }
    return result;
}

std::string ReflectionBuilder::fieldPath(const std::string& basePath, const std::string& fieldName) const {
    if (basePath.empty()) {
        return fieldName;
    }
    return basePath + "." + fieldName;
}

} // namespace generator
} // namespace rybench

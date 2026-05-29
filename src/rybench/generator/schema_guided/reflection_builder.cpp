#include "reflection_builder.hpp"

namespace rybench::generator {

ReflectionBuilder::ReflectionBuilder(const core::FlatBuffersSchema& schema)
    : schema_(schema) {}

core::Message ReflectionBuilder::build(const Samplers& samplers) {
    if (!schema_.isLoaded()) return core::Message();

    auto* rootTable = schema_.rootTable();
    if (!rootTable) return core::Message();

    samplers_ = &samplers;

    flatbuffers::FlatBufferBuilder builder(1024);
    auto rootOffset = buildTable(builder, rootTable, schema_.fields(), "");
    builder.Finish(flatbuffers::Offset<flatbuffers::Table>(rootOffset.o));

    auto* buf = builder.GetBufferPointer();
    auto size = builder.GetSize();
    std::vector<uint8_t> buffer(buf, buf + size);

    samplers_ = nullptr;

    return core::Message(std::move(buffer), core::MessageFormat::FlatBuffers,
                         schema_.reflectionSchema());
}

flatbuffers::Offset<void> ReflectionBuilder::buildTable(
    flatbuffers::FlatBufferBuilder& builder,
    const reflection::Object* table,
    const std::vector<core::FbsSchemaField>& fields,
    const std::string& path) {

    if (!table->fields()) return flatbuffers::Offset<void>(0);

    std::vector<std::pair<uint16_t, flatbuffers::Offset<void>>> offsets;

    for (const auto& sf : fields) {
        auto* field = table->fields()->LookupByKey(sf.name.c_str());
        if (!field) continue;

        std::string fPath = fieldPath(path, sf.name);
        auto baseType = field->type()->base_type();

        if (baseType == reflection::BaseType::String) {
            offsets.emplace_back(field->offset(),
                buildString(builder, sf.constraints, fPath).Union());
        } else if (baseType == reflection::BaseType::Vector) {
            offsets.emplace_back(field->offset(),
                buildVector(builder, field, sf, fPath));
        } else if (baseType == reflection::BaseType::Obj) {
            auto* nestedObj = schema_.reflectionSchema()->objects()->Get(field->type()->index());
            if (nestedObj && !nestedObj->is_struct()) {
                offsets.emplace_back(field->offset(),
                    buildTable(builder, nestedObj, sf.children, fPath));
            }
        }
    }

    auto start = builder.StartTable();

    for (const auto& [id, offset] : offsets) {
        builder.AddOffset(id, offset);
    }

    for (const auto& sf : fields) {
        auto* field = table->fields()->LookupByKey(sf.name.c_str());
        if (!field) continue;

        std::string fPath = fieldPath(path, sf.name);
        auto baseType = field->type()->base_type();

        int32_t enumIndex = field->type()->index();
        bool isEnum = (enumIndex >= 0) &&
                      (baseType == reflection::BaseType::Byte  ||
                       baseType == reflection::BaseType::UByte ||
                       baseType == reflection::BaseType::Short ||
                       baseType == reflection::BaseType::UShort||
                       baseType == reflection::BaseType::Int   ||
                       baseType == reflection::BaseType::UInt);

        if (isEnum && schema_.reflectionSchema()->enums()) {
            auto* enumDef = schema_.reflectionSchema()->enums()->Get(enumIndex);
            if (enumDef && enumDef->values() && enumDef->values()->size() > 0) {
                int64_t enumValue = samplers_->sampleEnum(enumDef);
                switch (baseType) {
                    case reflection::BaseType::Byte:
                        builder.AddElement<int8_t>(field->offset(),
                            static_cast<int8_t>(enumValue), static_cast<int8_t>(field->default_integer()));
                        break;
                    case reflection::BaseType::UByte:
                        builder.AddElement<uint8_t>(field->offset(),
                            static_cast<uint8_t>(enumValue), static_cast<uint8_t>(field->default_integer()));
                        break;
                    case reflection::BaseType::Short:
                        builder.AddElement<int16_t>(field->offset(),
                            static_cast<int16_t>(enumValue), static_cast<int16_t>(field->default_integer()));
                        break;
                    case reflection::BaseType::UShort:
                        builder.AddElement<uint16_t>(field->offset(),
                            static_cast<uint16_t>(enumValue), static_cast<uint16_t>(field->default_integer()));
                        break;
                    case reflection::BaseType::Int:
                        builder.AddElement<int32_t>(field->offset(),
                            static_cast<int32_t>(enumValue), static_cast<int32_t>(field->default_integer()));
                        break;
                    case reflection::BaseType::UInt:
                        builder.AddElement<uint32_t>(field->offset(),
                            static_cast<uint32_t>(enumValue), static_cast<uint32_t>(field->default_integer()));
                        break;
                    default: break;
                }
                continue;
            }
        }

        switch (baseType) {
            case reflection::BaseType::Bool:
                builder.AddElement<uint8_t>(field->offset(),
                    samplers_->sampleBool() ? 1 : 0,
                    static_cast<uint8_t>(field->default_integer()));
                break;
            case reflection::BaseType::Byte:
                builder.AddElement<int8_t>(field->offset(),
                    static_cast<int8_t>(samplers_->sampleInt(sf.constraints, fPath, false)),
                    static_cast<int8_t>(field->default_integer()));
                break;
            case reflection::BaseType::UByte:
                builder.AddElement<uint8_t>(field->offset(),
                    static_cast<uint8_t>(samplers_->sampleInt(sf.constraints, fPath, true)),
                    static_cast<uint8_t>(field->default_integer()));
                break;
            case reflection::BaseType::Short:
                builder.AddElement<int16_t>(field->offset(),
                    static_cast<int16_t>(samplers_->sampleInt(sf.constraints, fPath, false)),
                    static_cast<int16_t>(field->default_integer()));
                break;
            case reflection::BaseType::UShort:
                builder.AddElement<uint16_t>(field->offset(),
                    static_cast<uint16_t>(samplers_->sampleInt(sf.constraints, fPath, true)),
                    static_cast<uint16_t>(field->default_integer()));
                break;
            case reflection::BaseType::Int:
                builder.AddElement<int32_t>(field->offset(),
                    static_cast<int32_t>(samplers_->sampleInt(sf.constraints, fPath, false)),
                    static_cast<int32_t>(field->default_integer()));
                break;
            case reflection::BaseType::UInt:
                builder.AddElement<uint32_t>(field->offset(),
                    static_cast<uint32_t>(samplers_->sampleInt(sf.constraints, fPath, true)),
                    static_cast<uint32_t>(field->default_integer()));
                break;
            case reflection::BaseType::Long:
                builder.AddElement<int64_t>(field->offset(),
                    samplers_->sampleInt(sf.constraints, fPath, false),
                    field->default_integer());
                break;
            case reflection::BaseType::ULong:
                builder.AddElement<uint64_t>(field->offset(),
                    static_cast<uint64_t>(samplers_->sampleInt(sf.constraints, fPath, true)),
                    static_cast<uint64_t>(field->default_integer()));
                break;
            case reflection::BaseType::Float:
                builder.AddElement<float>(field->offset(),
                    static_cast<float>(samplers_->sampleDouble(sf.constraints, fPath)),
                    static_cast<float>(field->default_real()));
                break;
            case reflection::BaseType::Double:
                builder.AddElement<double>(field->offset(),
                    samplers_->sampleDouble(sf.constraints, fPath),
                    field->default_real());
                break;
            case reflection::BaseType::Obj:
                if (field->type()->index() >= 0) {
                    auto* obj = schema_.reflectionSchema()->objects()->Get(field->type()->index());
                    if (obj && obj->is_struct()) {
                        buildStruct(builder, obj, sf.children, fPath);
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
    const core::FbsFieldConstraints& constraints,
    const std::string& path) {

    return builder.CreateString(samplers_->sampleString(constraints, path));
}

flatbuffers::Offset<void> ReflectionBuilder::buildVector(
    flatbuffers::FlatBufferBuilder& builder,
    const reflection::Field* field,
    const core::FbsSchemaField& schemaField,
    const std::string& path) {

    size_t minItems = schemaField.constraints.minItems.value_or(1);
    size_t maxItems = schemaField.constraints.maxItems.value_or(5);
    size_t count = samplers_->sampleVectorSize(minItems, maxItems);

    auto elementType = field->type()->element();

    if (elementType == reflection::BaseType::Obj) {
        int32_t objIndex = field->type()->index();
        if (objIndex >= 0) {
            auto* elementObj = schema_.reflectionSchema()->objects()->Get(objIndex);
            if (elementObj && !elementObj->is_struct()) {
                std::vector<flatbuffers::Offset<flatbuffers::Table>> tableOffsets;
                for (size_t i = 0; i < count; ++i) {
                    std::string elemPath = path + "[" + std::to_string(i) + "]";
                    auto& elementFields = schemaField.children.empty() ?
                        std::vector<core::FbsSchemaField>() : schemaField.children[0].children;
                    auto offset = buildTable(builder, elementObj, elementFields, elemPath);
                    tableOffsets.push_back(flatbuffers::Offset<flatbuffers::Table>(offset.o));
                }
                return builder.CreateVector(tableOffsets).Union();
            }
        }
    } else if (elementType == reflection::BaseType::String) {
        std::vector<flatbuffers::Offset<flatbuffers::String>> strOffsets;
        core::FbsFieldConstraints elemConstraints;
        if (!schemaField.children.empty()) elemConstraints = schemaField.children[0].constraints;
        for (size_t i = 0; i < count; ++i) {
            strOffsets.push_back(buildString(builder, elemConstraints,
                path + "[" + std::to_string(i) + "]"));
        }
        return builder.CreateVector(strOffsets).Union();
    } else {
        core::FbsFieldConstraints elemConstraints;
        if (!schemaField.children.empty()) elemConstraints = schemaField.children[0].constraints;

        switch (elementType) {
            case reflection::BaseType::Int: {
                std::vector<int32_t> v;
                for (size_t i = 0; i < count; ++i)
                    v.push_back(static_cast<int32_t>(samplers_->sampleInt(elemConstraints, path, false)));
                return builder.CreateVector(v).Union();
            }
            case reflection::BaseType::UInt: {
                std::vector<uint32_t> v;
                for (size_t i = 0; i < count; ++i)
                    v.push_back(static_cast<uint32_t>(samplers_->sampleInt(elemConstraints, path, true)));
                return builder.CreateVector(v).Union();
            }
            case reflection::BaseType::Long: {
                std::vector<int64_t> v;
                for (size_t i = 0; i < count; ++i)
                    v.push_back(samplers_->sampleInt(elemConstraints, path, false));
                return builder.CreateVector(v).Union();
            }
            case reflection::BaseType::ULong: {
                std::vector<uint64_t> v;
                for (size_t i = 0; i < count; ++i)
                    v.push_back(static_cast<uint64_t>(samplers_->sampleInt(elemConstraints, path, true)));
                return builder.CreateVector(v).Union();
            }
            case reflection::BaseType::Float: {
                std::vector<float> v;
                for (size_t i = 0; i < count; ++i)
                    v.push_back(static_cast<float>(samplers_->sampleDouble(elemConstraints, path)));
                return builder.CreateVector(v).Union();
            }
            case reflection::BaseType::Double: {
                std::vector<double> v;
                for (size_t i = 0; i < count; ++i)
                    v.push_back(samplers_->sampleDouble(elemConstraints, path));
                return builder.CreateVector(v).Union();
            }
            case reflection::BaseType::Bool: {
                std::vector<uint8_t> v;
                for (size_t i = 0; i < count; ++i)
                    v.push_back(samplers_->sampleBool() ? 1 : 0);
                return builder.CreateVector(v).Union();
            }
            default: break;
        }
    }

    return flatbuffers::Offset<void>(0);
}

flatbuffers::Offset<void> ReflectionBuilder::buildStruct(
    flatbuffers::FlatBufferBuilder& builder,
    const reflection::Object* structDef,
    const std::vector<core::FbsSchemaField>& fields,
    const std::string& path) {

    (void)builder; (void)structDef; (void)fields; (void)path;
    return flatbuffers::Offset<void>(0);
}

std::string ReflectionBuilder::fieldPath(const std::string& basePath, const std::string& fieldName) const {
    if (basePath.empty()) return fieldName;
    return basePath + "." + fieldName;
}

} // namespace rybench::generator

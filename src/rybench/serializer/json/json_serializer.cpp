#include "json_serializer.hpp"
#include <stdexcept>
#ifdef RYBENCH_ENABLE_FLATBUFFERS
#include <flatbuffers/flatbuffers.h>
#endif
#include <iostream>

namespace rybench::serializer {

JsonSerializer::JsonSerializer(const std::string& bfbsPath) {
#ifdef RYBENCH_ENABLE_FLATBUFFERS
    if (!bfbsPath.empty()) {
        try {
            fbsSchema_.loadFromBfbs(bfbsPath);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to load bfbs schema: " << e.what() << std::endl;
        }
    }
#endif
}

std::string JsonSerializer::name() const {
    return "json";
}

core::MessageFormat JsonSerializer::format() const {
    return core::MessageFormat::Json;
}

std::span<const uint8_t> JsonSerializer::serialize(const core::Message& msg) {
    if (msg.format() == core::MessageFormat::Json) {
        return msg.bytes();
    }
#ifdef RYBENCH_ENABLE_FLATBUFFERS
    if (msg.format() == core::MessageFormat::FlatBuffers) {
        rapidjson::StringBuffer strbuf;
        rapidjson::Writer<rapidjson::StringBuffer> writer(strbuf);
        flatbufferToJson(writer, msg);
        const auto* str = reinterpret_cast<const uint8_t*>(strbuf.GetString());
        buffer_.assign(str, str + strbuf.GetSize());
        return buffer_;
    }
#endif
    throw std::runtime_error(
        std::string("Unsupported conversion: ")
        + std::string(core::toString(msg.format()))
        + " -> json");
}

#ifdef RYBENCH_ENABLE_FLATBUFFERS
void JsonSerializer::flatbufferToJson(rapidjson::Writer<rapidjson::StringBuffer>& writer, const core::Message& msg) {
    if (msg.empty()) {
        writer.StartObject();
        writer.EndObject();
        return;
    }

    const reflection::Schema* schema = msg.schema();
    if (!schema) {
        schema = fbsSchema_.reflectionSchema();
    }

    if (!schema || !schema->root_table()) {
        writer.StartObject();
        writer.EndObject();
        return;
    }

    auto* rootTable = flatbuffers::GetAnyRoot(msg.data());
    tableToJson(writer, schema->root_table(), rootTable);
}

void JsonSerializer::tableToJson(rapidjson::Writer<rapidjson::StringBuffer>& writer, const reflection::Object* obj, const flatbuffers::Table* table) {
    writer.StartObject();

    if (!obj || !table || !obj->fields()) {
        writer.EndObject();
        return;
    }

    for (const auto* field : *obj->fields()) {
        writer.Key(field->name()->c_str());
        valueToJson(writer, field, table);
    }

    writer.EndObject();
}

void JsonSerializer::valueToJson(rapidjson::Writer<rapidjson::StringBuffer>& writer, const reflection::Field* field, const flatbuffers::Table* table) {
    auto baseType = field->type()->base_type();
    const reflection::Schema* schema = fbsSchema_.reflectionSchema();

    switch (baseType) {
        case reflection::BaseType::Bool:
            writer.Bool(table->GetField<uint8_t>(field->offset(), 0) != 0);
            break;
        case reflection::BaseType::Byte:
            writer.Int(table->GetField<int8_t>(field->offset(), 0));
            break;
        case reflection::BaseType::UByte:
            writer.Uint(table->GetField<uint8_t>(field->offset(), 0));
            break;
        case reflection::BaseType::Short:
            writer.Int(table->GetField<int16_t>(field->offset(), 0));
            break;
        case reflection::BaseType::UShort:
            writer.Uint(table->GetField<uint16_t>(field->offset(), 0));
            break;
        case reflection::BaseType::Int:
            writer.Int(table->GetField<int32_t>(field->offset(), 0));
            break;
        case reflection::BaseType::UInt:
            writer.Uint(table->GetField<uint32_t>(field->offset(), 0));
            break;
        case reflection::BaseType::Long:
            writer.Int64(table->GetField<int64_t>(field->offset(), 0));
            break;
        case reflection::BaseType::ULong:
            writer.Uint64(table->GetField<uint64_t>(field->offset(), 0));
            break;
        case reflection::BaseType::Float:
            writer.Double(table->GetField<float>(field->offset(), 0.0f));
            break;
        case reflection::BaseType::Double:
            writer.Double(table->GetField<double>(field->offset(), 0.0));
            break;
        case reflection::BaseType::String: {
            auto str = table->GetPointer<const flatbuffers::String*>(field->offset());
            writer.String(str ? str->c_str() : "");
            break;
        }
        case reflection::BaseType::Obj: {
            auto* nestedTable = table->GetPointer<const flatbuffers::Table*>(field->offset());
            if (nestedTable && field->type()->index() >= 0 && schema) {
                auto* nestedObj = schema->objects()->Get(field->type()->index());
                if (nestedObj) {
                    tableToJson(writer, nestedObj, nestedTable);
                    break;
                }
            }
            writer.Null();
            break;
        }
        case reflection::BaseType::Vector: {
            writer.StartArray();
            auto elementType = field->type()->element();

            if (elementType == reflection::BaseType::Obj) {
                auto* vec = table->GetPointer<const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::Table>>*>(field->offset());
                if (vec && field->type()->index() >= 0 && schema) {
                    auto* elemObj = schema->objects()->Get(field->type()->index());
                    for (size_t i = 0; i < vec->size(); ++i) {
                        tableToJson(writer, elemObj, vec->Get(i));
                    }
                }
            } else if (elementType == reflection::BaseType::String) {
                auto* vec = table->GetPointer<const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>*>(field->offset());
                if (vec) {
                    for (size_t i = 0; i < vec->size(); ++i) {
                        writer.String(vec->Get(i)->c_str());
                    }
                }
            } else {
                switch (elementType) {
                    case reflection::BaseType::Int: {
                        auto* vec = table->GetPointer<const flatbuffers::Vector<int32_t>*>(field->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) writer.Int(vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::UInt: {
                        auto* vec = table->GetPointer<const flatbuffers::Vector<uint32_t>*>(field->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) writer.Uint(vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::Long: {
                        auto* vec = table->GetPointer<const flatbuffers::Vector<int64_t>*>(field->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) writer.Int64(vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::ULong: {
                        auto* vec = table->GetPointer<const flatbuffers::Vector<uint64_t>*>(field->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) writer.Uint64(vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::Float: {
                        auto* vec = table->GetPointer<const flatbuffers::Vector<float>*>(field->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) writer.Double(vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::Double: {
                        auto* vec = table->GetPointer<const flatbuffers::Vector<double>*>(field->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) writer.Double(vec->Get(i));
                        break;
                    }
                    default:
                        break;
                }
            }
            writer.EndArray();
            break;
        }
        default:
            writer.Null();
            break;
    }
}
#endif // RYBENCH_ENABLE_FLATBUFFERS

void JsonSerializer::loadSchema(const core::SchemaDefinition&) {}
void JsonSerializer::initMessage() {}
void JsonSerializer::addBool  (const core::SchemaField&, bool) {}
void JsonSerializer::addInt   (const core::SchemaField&, int64_t) {}
void JsonSerializer::addUInt  (const core::SchemaField&, uint64_t) {}
void JsonSerializer::addFloat (const core::SchemaField&, float) {}
void JsonSerializer::addDouble(const core::SchemaField&, double) {}
void JsonSerializer::addString(const core::SchemaField&, std::string_view) {}
void JsonSerializer::beginTable (const core::SchemaField&) {}
void JsonSerializer::endTable   () {}
void JsonSerializer::beginVector(const core::SchemaField&, size_t) {}
void JsonSerializer::endVector  () {}
core::Message JsonSerializer::finalizeMessage() {
    throw std::runtime_error("JsonSerializer::finalizeMessage: builder API not yet implemented");
}

} // namespace rybench::serializer

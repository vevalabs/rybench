#include "json_serializer.hpp"
#include <flatbuffers/flatbuffers.h>
#include <iostream>

namespace rybench {
namespace serializer {

JsonSerializer::JsonSerializer(const std::string& bfbsPath) {
    if (!bfbsPath.empty()) {
        try {
            fbsSchema_.loadFromBfbs(bfbsPath);
        } catch (const std::exception& e) {
            std::cerr << "Warning: Failed to load bfbs schema: " << e.what() << std::endl;
        }
    }
}

std::string JsonSerializer::name() const {
    return "json";
}

std::vector<uint8_t> JsonSerializer::serialize(const common::Message& msg) {
    nlohmann::json j = flatbufferToJson(msg);
    std::string str = j.dump();
    return std::vector<uint8_t>(str.begin(), str.end());
}

nlohmann::json JsonSerializer::flatbufferToJson(const common::Message& msg) {
    if (msg.empty()) {
        return nlohmann::json::object();
    }

    const reflection::Schema* schema = msg.schema();
    if (!schema) {
        schema = fbsSchema_.reflectionSchema();
    }

    if (!schema || !schema->root_table()) {
        return nlohmann::json::object();
    }

    auto* rootTable = flatbuffers::GetAnyRoot(msg.data());
    return tableToJson(schema->root_table(), rootTable);
}

nlohmann::json JsonSerializer::tableToJson(const reflection::Object* obj, const flatbuffers::Table* table) {
    nlohmann::json result = nlohmann::json::object();

    if (!obj || !table || !obj->fields()) {
        return result;
    }

    for (const auto* field : *obj->fields()) {
        result[field->name()->c_str()] = valueToJson(field, table);
    }

    return result;
}

nlohmann::json JsonSerializer::valueToJson(const reflection::Field* field, const flatbuffers::Table* table) {
    auto baseType = field->type()->base_type();
    const reflection::Schema* schema = fbsSchema_.reflectionSchema();

    switch (baseType) {
        case reflection::BaseType::Bool:
            return table->GetField<uint8_t>(field->offset(), 0) != 0;
        case reflection::BaseType::Byte:
            return static_cast<int>(table->GetField<int8_t>(field->offset(), 0));
        case reflection::BaseType::UByte:
            return static_cast<unsigned>(table->GetField<uint8_t>(field->offset(), 0));
        case reflection::BaseType::Short:
            return table->GetField<int16_t>(field->offset(), 0);
        case reflection::BaseType::UShort:
            return table->GetField<uint16_t>(field->offset(), 0);
        case reflection::BaseType::Int:
            return table->GetField<int32_t>(field->offset(), 0);
        case reflection::BaseType::UInt:
            return table->GetField<uint32_t>(field->offset(), 0);
        case reflection::BaseType::Long:
            return table->GetField<int64_t>(field->offset(), 0);
        case reflection::BaseType::ULong:
            return table->GetField<uint64_t>(field->offset(), 0);
        case reflection::BaseType::Float:
            return table->GetField<float>(field->offset(), 0.0f);
        case reflection::BaseType::Double:
            return table->GetField<double>(field->offset(), 0.0);
        case reflection::BaseType::String: {
            auto str = table->GetPointer<const flatbuffers::String*>(field->offset());
            return str ? str->c_str() : "";
        }
        case reflection::BaseType::Obj: {
            auto* nestedTable = table->GetPointer<const flatbuffers::Table*>(field->offset());
            if (nestedTable && field->type()->index() >= 0 && schema) {
                auto* nestedObj = schema->objects()->Get(field->type()->index());
                if (nestedObj) {
                    return tableToJson(nestedObj, nestedTable);
                }
            }
            return nullptr;
        }
        case reflection::BaseType::Vector: {
            nlohmann::json arr = nlohmann::json::array();
            auto elementType = field->type()->element();

            if (elementType == reflection::BaseType::Obj) {
                auto* vec = table->GetPointer<const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::Table>>*>(field->offset());
                if (vec && field->type()->index() >= 0 && schema) {
                    auto* elemObj = schema->objects()->Get(field->type()->index());
                    for (size_t i = 0; i < vec->size(); ++i) {
                        arr.push_back(tableToJson(elemObj, vec->Get(i)));
                    }
                }
            } else if (elementType == reflection::BaseType::String) {
                auto* vec = table->GetPointer<const flatbuffers::Vector<flatbuffers::Offset<flatbuffers::String>>*>(field->offset());
                if (vec) {
                    for (size_t i = 0; i < vec->size(); ++i) {
                        arr.push_back(vec->Get(i)->c_str());
                    }
                }
            } else {
                // Scalar vectors
                switch (elementType) {
                    case reflection::BaseType::Int: {
                        auto* vec = table->GetPointer<const flatbuffers::Vector<int32_t>*>(field->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) arr.push_back(vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::UInt: {
                        auto* vec = table->GetPointer<const flatbuffers::Vector<uint32_t>*>(field->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) arr.push_back(vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::Long: {
                        auto* vec = table->GetPointer<const flatbuffers::Vector<int64_t>*>(field->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) arr.push_back(vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::ULong: {
                        auto* vec = table->GetPointer<const flatbuffers::Vector<uint64_t>*>(field->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) arr.push_back(vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::Float: {
                        auto* vec = table->GetPointer<const flatbuffers::Vector<float>*>(field->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) arr.push_back(vec->Get(i));
                        break;
                    }
                    case reflection::BaseType::Double: {
                        auto* vec = table->GetPointer<const flatbuffers::Vector<double>*>(field->offset());
                        if (vec) for (size_t i = 0; i < vec->size(); ++i) arr.push_back(vec->Get(i));
                        break;
                    }
                    default:
                        break;
                }
            }
            return arr;
        }
        default:
            return nullptr;
    }
}

} // namespace serializer
} // namespace rybench

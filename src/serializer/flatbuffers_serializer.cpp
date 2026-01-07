#include "flatbuffers_serializer.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

namespace serializer {

std::unordered_map<std::string, FlatbuffersSerializer::MessageHandler>& FlatbuffersSerializer::registry() {
    static std::unordered_map<std::string, MessageHandler> instance;
    return instance;
}

FlatbuffersSerializer::FlatbuffersSerializer(const std::string& messageName,
                                             const std::string& bfbsPath) {
    messageName_ = messageName.empty() ? "osi::SensorViewMessage" : messageName;

    auto it = registry().find(messageName_);
    if (it == registry().end()) {
        throw std::runtime_error("Unknown flatbuffers message type: " + messageName_ +
                                 ". Available: " + availableMessagesStr());
    }
    handler_ = &it->second;

    // Load binary schema if provided
    if (!bfbsPath.empty()) {
        loadBinarySchema(bfbsPath);
    }
}

void FlatbuffersSerializer::loadBinarySchema(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        std::cerr << "Warning: Failed to open binary schema: " << path << std::endl;
        return;
    }

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    bfbsData_.resize(size);
    if (!file.read(reinterpret_cast<char*>(bfbsData_.data()), size)) {
        std::cerr << "Warning: Failed to read binary schema: " << path << std::endl;
        bfbsData_.clear();
        return;
    }

    std::cout << "Loaded FlatBuffers schema: " << path << std::endl;
}

common::SchemaField FlatbuffersSerializer::reflectionFieldToSchema(
    const reflection::Field* field,
    const reflection::Schema* schema) const {

    common::SchemaField sf;
    sf.name = field->name()->str();

    auto baseType = field->type()->base_type();

    // Handle scalar types
    switch (baseType) {
        case reflection::Bool:
            sf.type = common::FieldType::Bool;
            break;
        case reflection::Byte:
        case reflection::UByte:
        case reflection::Short:
        case reflection::UShort:
        case reflection::Int:
        case reflection::UInt:
        case reflection::Long:
        case reflection::ULong:
            sf.type = common::FieldType::Int;
            sf.constraints.min = 0;
            sf.constraints.max = 1000;
            break;
        case reflection::Float:
        case reflection::Double:
            sf.type = common::FieldType::Double;
            sf.constraints.min = -100.0;
            sf.constraints.max = 100.0;
            break;
        case reflection::String:
            sf.type = common::FieldType::String;
            sf.constraints.minLength = 5;
            sf.constraints.maxLength = 20;
            break;
        case reflection::Vector: {
            sf.type = common::FieldType::Array;
            sf.constraints.arraySize = 3;

            // Get element type
            auto elemType = field->type()->element();
            common::SchemaField itemField;
            itemField.name = "item";

            if (elemType == reflection::Obj) {
                itemField.type = common::FieldType::Object;
                auto idx = field->type()->index();
                if (idx >= 0 && idx < static_cast<int>(schema->objects()->size())) {
                    auto* obj = schema->objects()->Get(idx);
                    if (obj->fields()) {
                        for (auto* f : *obj->fields()) {
                            itemField.children.push_back(reflectionFieldToSchema(f, schema));
                        }
                    }
                }
            } else {
                switch (elemType) {
                    case reflection::Int:
                    case reflection::Long:
                        itemField.type = common::FieldType::Int;
                        break;
                    case reflection::Float:
                    case reflection::Double:
                        itemField.type = common::FieldType::Double;
                        break;
                    case reflection::String:
                        itemField.type = common::FieldType::String;
                        break;
                    default:
                        itemField.type = common::FieldType::Int;
                }
            }
            sf.children.push_back(std::move(itemField));
            break;
        }
        case reflection::Obj: {
            sf.type = common::FieldType::Object;
            auto idx = field->type()->index();
            if (idx >= 0 && idx < static_cast<int>(schema->objects()->size())) {
                auto* obj = schema->objects()->Get(idx);
                if (obj->fields()) {
                    for (auto* f : *obj->fields()) {
                        sf.children.push_back(reflectionFieldToSchema(f, schema));
                    }
                }
            }
            break;
        }
        default:
            sf.type = common::FieldType::Int;
    }

    return sf;
}

std::vector<std::string> FlatbuffersSerializer::availableMessages() {
    std::vector<std::string> messages;
    for (const auto& [name, _] : registry()) {
        messages.push_back(name);
    }
    return messages;
}

std::string FlatbuffersSerializer::availableMessagesStr() {
    std::string result;
    for (const auto& [name, _] : registry()) {
        if (!result.empty()) result += ", ";
        result += name;
    }
    return result.empty() ? "(none registered)" : result;
}

std::vector<uint8_t> FlatbuffersSerializer::serialize(const common::DataValue& value) {
    return handler_->build(value);
}

common::DataValue FlatbuffersSerializer::deserialize(const std::vector<uint8_t>& data) {
    return handler_->parse(data);
}

namespace {
// Helper to convert SchemaField to JSON
nlohmann::json schemaFieldToJson(const common::SchemaField& sf) {
    nlohmann::json j;
    j["name"] = sf.name;

    switch (sf.type) {
        case common::FieldType::Int: j["type"] = "int"; break;
        case common::FieldType::Double: j["type"] = "double"; break;
        case common::FieldType::String: j["type"] = "string"; break;
        case common::FieldType::Bool: j["type"] = "bool"; break;
        case common::FieldType::Array: j["type"] = "array"; break;
        case common::FieldType::Object: j["type"] = "object"; break;
    }

    if (sf.constraints.min) j["min"] = *sf.constraints.min;
    if (sf.constraints.max) j["max"] = *sf.constraints.max;
    if (sf.constraints.minLength) j["minLength"] = *sf.constraints.minLength;
    if (sf.constraints.maxLength) j["maxLength"] = *sf.constraints.maxLength;
    if (sf.constraints.arraySize) j["arraySize"] = *sf.constraints.arraySize;

    if (sf.type == common::FieldType::Object && !sf.children.empty()) {
        j["fields"] = nlohmann::json::array();
        for (const auto& child : sf.children) {
            j["fields"].push_back(schemaFieldToJson(child));
        }
    } else if (sf.type == common::FieldType::Array && !sf.children.empty()) {
        j["items"] = schemaFieldToJson(sf.children[0]);
    }

    return j;
}
} // anonymous namespace

common::Schema FlatbuffersSerializer::getSchema() const {
    // If we have binary schema data, use reflection
    if (!bfbsData_.empty()) {
        auto* reflSchema = reflection::GetSchema(bfbsData_.data());
        if (reflSchema && reflSchema->root_table()) {
            auto* rootTable = reflSchema->root_table();

            nlohmann::json j;
            j["name"] = rootTable->name()->str();
            j["fields"] = nlohmann::json::array();

            if (rootTable->fields()) {
                for (auto* field : *rootTable->fields()) {
                    auto sf = reflectionFieldToSchema(field, reflSchema);
                    j["fields"].push_back(schemaFieldToJson(sf));
                }
            }

            common::Schema schema;
            schema.loadFromJson(j);
            return schema;
        }
    }

    // Fall back to stored schema
    return schema_;
}

} // namespace serializer

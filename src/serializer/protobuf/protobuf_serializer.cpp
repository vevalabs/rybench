#include "protobuf_serializer.hpp"
#include <stdexcept>
#include <sstream>
#include <cctype>
#include <nlohmann/json.hpp>

namespace serializer {

std::unordered_map<std::string, ProtobufSerializer::MessageFactory>& ProtobufSerializer::registry() {
    static std::unordered_map<std::string, MessageFactory> instance;
    return instance;
}

ProtobufSerializer::ProtobufSerializer(const std::string& messageName,
                                       const std::string& fieldsConfigPath) {
    if (messageName.empty()) {
        messageName_ = "osi::SensorViewMessage";
    } else {
        messageName_ = messageName;
    }

    auto it = registry().find(messageName_);
    if (it == registry().end()) {
        throw std::runtime_error("Unknown protobuf message type: " + messageName_ +
                                 ". Available: " + availableMessagesStr());
    }
    factory_ = it->second;

    if (!fieldsConfigPath.empty()) {
        loadFieldsConfig(fieldsConfigPath);
    }
}

std::string ProtobufSerializer::name() const {
    return "protobuf";
}

void ProtobufSerializer::loadFieldsConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return;

    std::string line, currentSection;
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t"));
        if (line.empty() || line[0] == '#') continue;

        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            continue;
        }

        auto eqPos = line.find('=');
        if (eqPos == std::string::npos) continue;

        std::string key = line.substr(0, eqPos);
        std::string value = line.substr(eqPos + 1);
        key.erase(key.find_last_not_of(" \t") + 1);
        key.erase(0, key.find_first_not_of(" \t"));
        value.erase(0, value.find_first_not_of(" \t"));

        std::string fullPath = currentSection.empty() ? key : currentSection + "." + key;
        common::FieldConstraints& c = fieldConstraints_[fullPath];

        if (key.find(".length") != std::string::npos) {
            std::string basePath = fullPath.substr(0, fullPath.find(".length"));
            auto& bc = fieldConstraints_[basePath];
            int minLen, maxLen;
            sscanf(value.c_str(), "%d, %d", &minLen, &maxLen);
            bc.minLength = minLen;
            bc.maxLength = maxLen;
        } else if (key.find(".count") != std::string::npos) {
            std::string basePath = fullPath.substr(0, fullPath.find(".count"));
            auto& bc = fieldConstraints_[basePath];
            int minCount, maxCount;
            sscanf(value.c_str(), "%d, %d", &minCount, &maxCount);
            bc.arraySize = maxCount;
        } else {
            double minVal, maxVal;
            sscanf(value.c_str(), "%lf, %lf", &minVal, &maxVal);
            c.min = minVal;
            c.max = maxVal;
        }
    }
}

common::FieldConstraints ProtobufSerializer::getConstraints(
    const std::string& path, const std::string& typeName) const {

    auto it = fieldConstraints_.find(path);
    if (it != fieldConstraints_.end()) return it->second;

    it = fieldConstraints_.find(typeName);
    if (it != fieldConstraints_.end()) return it->second;

    auto dotPos = path.rfind('.');
    if (dotPos != std::string::npos) {
        std::string fieldName = path.substr(dotPos + 1);
        it = fieldConstraints_.find(typeName + "." + fieldName);
        if (it != fieldConstraints_.end()) return it->second;
    }

    return common::FieldConstraints{};
}

common::SchemaField ProtobufSerializer::descriptorToSchemaField(
    const google::protobuf::FieldDescriptor* field,
    const std::string& path) const {

    common::SchemaField sf;
    sf.name = field->name();
    std::string fieldPath = path.empty() ? field->name() : path + "." + field->name();

    const auto* msgDesc = field->containing_type();
    std::string typeName = msgDesc ? msgDesc->name() : "";

    sf.constraints = getConstraints(fieldPath, typeName);

    switch (field->cpp_type()) {
        case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
        case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
        case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
        case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
            sf.type = common::FieldType::Int;
            if (!sf.constraints.min) sf.constraints.min = 0;
            if (!sf.constraints.max) sf.constraints.max = 1000;
            break;
        case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
        case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
            sf.type = common::FieldType::Double;
            if (!sf.constraints.min) sf.constraints.min = 0.0;
            if (!sf.constraints.max) sf.constraints.max = 100.0;
            break;
        case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
            sf.type = common::FieldType::Bool;
            break;
        case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
            sf.type = common::FieldType::String;
            if (!sf.constraints.minLength) sf.constraints.minLength = 5;
            if (!sf.constraints.maxLength) sf.constraints.maxLength = 20;
            break;
        case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
            sf.type = common::FieldType::Int;
            sf.constraints.min = 0;
            sf.constraints.max = field->enum_type()->value_count() - 1;
            break;
        case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
            sf.type = common::FieldType::Object;
            const auto* nested = field->message_type();
            for (int i = 0; i < nested->field_count(); ++i) {
                sf.children.push_back(descriptorToSchemaField(nested->field(i), fieldPath));
            }
            break;
        }
    }

    if (field->is_repeated()) {
        common::SchemaField arraySf;
        arraySf.name = sf.name;
        arraySf.type = common::FieldType::Array;
        arraySf.constraints.arraySize = sf.constraints.arraySize.value_or(3);
        arraySf.children.push_back(std::move(sf));
        arraySf.children[0].name = "item";
        return arraySf;
    }

    return sf;
}

namespace {
nlohmann::json schemaFieldToJson(const common::SchemaField& sf) {
    nlohmann::json fieldJson;
    fieldJson["name"] = sf.name;

    switch (sf.type) {
        case common::FieldType::Int: fieldJson["type"] = "int"; break;
        case common::FieldType::Double: fieldJson["type"] = "double"; break;
        case common::FieldType::String: fieldJson["type"] = "string"; break;
        case common::FieldType::Bool: fieldJson["type"] = "bool"; break;
        case common::FieldType::Array: fieldJson["type"] = "array"; break;
        case common::FieldType::Object: fieldJson["type"] = "object"; break;
    }

    if (sf.constraints.min) fieldJson["min"] = *sf.constraints.min;
    if (sf.constraints.max) fieldJson["max"] = *sf.constraints.max;
    if (sf.constraints.minLength) fieldJson["minLength"] = *sf.constraints.minLength;
    if (sf.constraints.maxLength) fieldJson["maxLength"] = *sf.constraints.maxLength;
    if (sf.constraints.arraySize) fieldJson["arraySize"] = *sf.constraints.arraySize;

    if (sf.type == common::FieldType::Object && !sf.children.empty()) {
        fieldJson["fields"] = nlohmann::json::array();
        for (const auto& child : sf.children) {
            fieldJson["fields"].push_back(schemaFieldToJson(child));
        }
    } else if (sf.type == common::FieldType::Array && !sf.children.empty()) {
        fieldJson["items"] = schemaFieldToJson(sf.children[0]);
    }

    return fieldJson;
}
} // anonymous namespace

common::Schema ProtobufSerializer::getSchema() const {
    common::Schema schema;

    auto msg = factory_();
    const auto* descriptor = msg->GetDescriptor();

    nlohmann::json j;
    j["name"] = descriptor->name();
    j["fields"] = nlohmann::json::array();

    for (int i = 0; i < descriptor->field_count(); ++i) {
        const auto* field = descriptor->field(i);
        auto sf = descriptorToSchemaField(field, "");
        j["fields"].push_back(schemaFieldToJson(sf));
    }

    schema.loadFromJson(j);
    return schema;
}

std::vector<std::string> ProtobufSerializer::availableMessages() {
    std::vector<std::string> messages;
    for (const auto& [name, _] : registry()) {
        messages.push_back(name);
    }
    return messages;
}

std::string ProtobufSerializer::availableMessagesStr() {
    std::string result;
    for (const auto& [name, _] : registry()) {
        if (!result.empty()) result += ", ";
        result += name;
    }
    return result;
}

ProtobufSerializer::MessageFactory ProtobufSerializer::getFactory(const std::string& name) {
    auto it = registry().find(name);
    if (it == registry().end()) {
        throw std::runtime_error("Unknown protobuf message type: " + name +
                                 ". Available: " + availableMessagesStr());
    }
    return it->second;
}

namespace {

void populateField(google::protobuf::Message* msg,
                   const google::protobuf::Reflection* reflection,
                   const google::protobuf::FieldDescriptor* field,
                   const common::DataValue& value);

void populateMessage(google::protobuf::Message* msg, const common::DataValue& value) {
    if (!value.isObject()) return;

    const auto& obj = value.asObject();
    const auto* descriptor = msg->GetDescriptor();
    const auto* reflection = msg->GetReflection();

    for (int i = 0; i < descriptor->field_count(); ++i) {
        const auto* field = descriptor->field(i);
        auto it = obj.find(field->name());
        if (it != obj.end()) {
            populateField(msg, reflection, field, it->second);
        }
    }
}

void populateField(google::protobuf::Message* msg,
                   const google::protobuf::Reflection* reflection,
                   const google::protobuf::FieldDescriptor* field,
                   const common::DataValue& value) {
    if (field->is_repeated()) {
        if (!value.isArray()) return;
        for (const auto& item : value.asArray()) {
            switch (field->cpp_type()) {
                case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                    reflection->AddInt32(msg, field, static_cast<int32_t>(item.asInt()));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                    reflection->AddInt64(msg, field, item.asInt());
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                    reflection->AddUInt32(msg, field, static_cast<uint32_t>(item.asInt()));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                    reflection->AddUInt64(msg, field, static_cast<uint64_t>(item.asInt()));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                    reflection->AddDouble(msg, field, item.asDouble());
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                    reflection->AddFloat(msg, field, static_cast<float>(item.asDouble()));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                    reflection->AddBool(msg, field, item.asBool());
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                    reflection->AddString(msg, field, item.asString());
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
                    int enumVal = static_cast<int>(item.asInt());
                    const auto* enumDesc = field->enum_type()->FindValueByNumber(enumVal);
                    if (enumDesc) reflection->AddEnum(msg, field, enumDesc);
                    break;
                }
                case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
                    auto* subMsg = reflection->AddMessage(msg, field);
                    populateMessage(subMsg, item);
                    break;
                }
            }
        }
    } else {
        switch (field->cpp_type()) {
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                reflection->SetInt32(msg, field, static_cast<int32_t>(value.asInt()));
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                reflection->SetInt64(msg, field, value.asInt());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                reflection->SetUInt32(msg, field, static_cast<uint32_t>(value.asInt()));
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                reflection->SetUInt64(msg, field, static_cast<uint64_t>(value.asInt()));
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                reflection->SetDouble(msg, field, value.asDouble());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                reflection->SetFloat(msg, field, static_cast<float>(value.asDouble()));
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                reflection->SetBool(msg, field, value.asBool());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                reflection->SetString(msg, field, value.asString());
                break;
            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM: {
                int enumVal = static_cast<int>(value.asInt());
                const auto* enumDesc = field->enum_type()->FindValueByNumber(enumVal);
                if (enumDesc) reflection->SetEnum(msg, field, enumDesc);
                break;
            }
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE: {
                auto* subMsg = reflection->MutableMessage(msg, field);
                populateMessage(subMsg, value);
                break;
            }
        }
    }
}

common::DataValue extractMessage(const google::protobuf::Message& msg);

common::DataValue extractField(const google::protobuf::Message& msg,
                                const google::protobuf::Reflection* reflection,
                                const google::protobuf::FieldDescriptor* field) {
    if (field->is_repeated()) {
        common::DataArray arr;
        int count = reflection->FieldSize(msg, field);
        for (int i = 0; i < count; ++i) {
            switch (field->cpp_type()) {
                case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                    arr.push_back(common::DataValue(static_cast<int64_t>(reflection->GetRepeatedInt32(msg, field, i))));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                    arr.push_back(common::DataValue(reflection->GetRepeatedInt64(msg, field, i)));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                    arr.push_back(common::DataValue(static_cast<int64_t>(reflection->GetRepeatedUInt32(msg, field, i))));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                    arr.push_back(common::DataValue(static_cast<int64_t>(reflection->GetRepeatedUInt64(msg, field, i))));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                    arr.push_back(common::DataValue(reflection->GetRepeatedDouble(msg, field, i)));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                    arr.push_back(common::DataValue(static_cast<double>(reflection->GetRepeatedFloat(msg, field, i))));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                    arr.push_back(common::DataValue(reflection->GetRepeatedBool(msg, field, i)));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                    arr.push_back(common::DataValue(reflection->GetRepeatedString(msg, field, i)));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                    arr.push_back(common::DataValue(static_cast<int64_t>(reflection->GetRepeatedEnumValue(msg, field, i))));
                    break;
                case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                    arr.push_back(extractMessage(reflection->GetRepeatedMessage(msg, field, i)));
                    break;
            }
        }
        return common::DataValue(std::move(arr));
    } else {
        switch (field->cpp_type()) {
            case google::protobuf::FieldDescriptor::CPPTYPE_INT32:
                return common::DataValue(static_cast<int64_t>(reflection->GetInt32(msg, field)));
            case google::protobuf::FieldDescriptor::CPPTYPE_INT64:
                return common::DataValue(reflection->GetInt64(msg, field));
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT32:
                return common::DataValue(static_cast<int64_t>(reflection->GetUInt32(msg, field)));
            case google::protobuf::FieldDescriptor::CPPTYPE_UINT64:
                return common::DataValue(static_cast<int64_t>(reflection->GetUInt64(msg, field)));
            case google::protobuf::FieldDescriptor::CPPTYPE_DOUBLE:
                return common::DataValue(reflection->GetDouble(msg, field));
            case google::protobuf::FieldDescriptor::CPPTYPE_FLOAT:
                return common::DataValue(static_cast<double>(reflection->GetFloat(msg, field)));
            case google::protobuf::FieldDescriptor::CPPTYPE_BOOL:
                return common::DataValue(reflection->GetBool(msg, field));
            case google::protobuf::FieldDescriptor::CPPTYPE_STRING:
                return common::DataValue(reflection->GetString(msg, field));
            case google::protobuf::FieldDescriptor::CPPTYPE_ENUM:
                return common::DataValue(static_cast<int64_t>(reflection->GetEnumValue(msg, field)));
            case google::protobuf::FieldDescriptor::CPPTYPE_MESSAGE:
                return extractMessage(reflection->GetMessage(msg, field));
        }
    }
    return common::DataValue(nullptr);
}

common::DataValue extractMessage(const google::protobuf::Message& msg) {
    const auto* descriptor = msg.GetDescriptor();
    const auto* reflection = msg.GetReflection();

    common::DataObject obj;
    for (int i = 0; i < descriptor->field_count(); ++i) {
        const auto* field = descriptor->field(i);
        obj[field->name()] = extractField(msg, reflection, field);
    }
    return common::DataValue(std::move(obj));
}

} // anonymous namespace

std::vector<uint8_t> ProtobufSerializer::serialize(const common::DataValue& value) {
    auto msg = factory_();
    populateMessage(msg.get(), value);

    const size_t n = static_cast<size_t>(msg->ByteSizeLong());
    std::vector<uint8_t> out(n);

    if (!msg->SerializeToArray(out.data(), static_cast<int>(n))) {
        return {};
    }

    return out;
}

common::DataValue ProtobufSerializer::deserialize(const std::vector<uint8_t>& data) {
    auto msg = factory_();
    if (!msg->ParseFromArray(data.data(), static_cast<int>(data.size()))) {
        throw std::runtime_error("Failed to parse protobuf message");
    }
    return extractMessage(*msg);
}

} // namespace serializer

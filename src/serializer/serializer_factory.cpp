#include "serializer_factory.hpp"
#include "json_serializer.hpp"
#include "protobuf_serializer.hpp"
#include "flatbuffers_serializer.hpp"

#include <iostream>
#include <filesystem>
#include <cctype>

namespace serializer {

namespace {

std::string deriveFieldsConfigPath(const std::string& messageName) {
    size_t colonPos = messageName.find("::");
    if (colonPos != std::string::npos) {
        std::string ns = messageName.substr(0, colonPos);
        std::string msg = messageName.substr(colonPos + 2);

        if (msg.size() > 7 && msg.substr(msg.size() - 7) == "Message") {
            msg = msg.substr(0, msg.size() - 7);
        }

        std::string filename = ns + "_";
        for (char c : msg) {
            filename += static_cast<char>(std::tolower(c));
        }
        return "config/protos/" + filename + ".fields";
    }
    return "";
}

} // anonymous namespace

SerializerPtr createSerializer(const common::Config& config) {
    std::string type = config.get("serializer", "type", "json");

    if (type == "json") {
        std::string schemaPath = config.get("generator", "schema", "");
        return std::make_unique<JsonSerializer>(schemaPath);
    }
    else if (type == "protobuf") {
        std::string messageName = config.get("serializer", "proto_message", "osi::SensorViewMessage");
        std::string fieldsConfig = deriveFieldsConfigPath(messageName);

        if (!fieldsConfig.empty() && std::filesystem::exists(fieldsConfig)) {
            std::cout << "Field constraints: " << fieldsConfig << std::endl;
        }

        return std::make_unique<ProtobufSerializer>(messageName, fieldsConfig);
    }
    else if (type == "flatbuffers") {
        std::string messageName = config.get("serializer", "fbs_message", "osi::SensorViewMessage");
        std::string bfbsPath = deriveFieldsConfigPath(messageName);
        if (!bfbsPath.empty()) {
            size_t pos = bfbsPath.find("/protos/");
            if (pos != std::string::npos) {
                bfbsPath.replace(pos, 8, "/flatbuffers/");
            }
            pos = bfbsPath.rfind(".fields");
            if (pos != std::string::npos) {
                bfbsPath.replace(pos, 7, ".bfbs");
            }
        }
        return std::make_unique<FlatbuffersSerializer>(messageName, bfbsPath);
    }
    else {
        std::cerr << "Unknown serializer type: " << type << ", falling back to json" << std::endl;
        return std::make_unique<JsonSerializer>();
    }
}

} // namespace serializer

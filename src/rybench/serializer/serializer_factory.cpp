#include <rybench/serializer/serializer_factory.hpp>
#include "json/json_serializer.hpp"
#include "protobuf/protobuf_serializer.hpp"
#include "flatbuffers/flatbuffers_serializer.hpp"

#include <iostream>

namespace rybench::serializer {

std::expected<SerializerPtr, std::string> createSerializer(const core::Config& config) {
    try {
        std::string type = config.get("serializer", "type", "json");
        std::string bfbsPath = config.get("schema", "bfbs", "");

        if (type == "json") {
            return std::make_unique<JsonSerializer>(bfbsPath);
        }
        if (type == "protobuf") {
            return std::make_unique<ProtobufSerializer>(bfbsPath);
        }
        if (type == "flatbuffers") {
            return std::make_unique<FlatbuffersSerializer>();
        }
        std::cerr << "Unknown serializer type: " << type << ", falling back to json\n";
        return std::make_unique<JsonSerializer>(bfbsPath);
    } catch (const std::exception& e) {
        return std::unexpected(std::string(e.what()));
    }
}

} // namespace rybench::serializer

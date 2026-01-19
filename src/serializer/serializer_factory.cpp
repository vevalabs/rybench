#include "serializer_factory.hpp"
#include "json_serializer.hpp"
#include "protobuf_serializer.hpp"
#include "flatbuffers_serializer.hpp"

#include <iostream>

namespace rybench {
namespace serializer {

SerializerPtr createSerializer(const common::Config& config) {
    std::string type = config.get("serializer", "type", "json");
    std::string bfbsPath = config.get("schema", "bfbs", "");

    if (type == "json") {
        return std::make_unique<JsonSerializer>(bfbsPath);
    }
    else if (type == "protobuf") {
        return std::make_unique<ProtobufSerializer>(bfbsPath);
    }
    else if (type == "flatbuffers") {
        return std::make_unique<FlatbuffersSerializer>();
    }
    else {
        std::cerr << "Unknown serializer type: " << type << ", falling back to json" << std::endl;
        return std::make_unique<JsonSerializer>(bfbsPath);
    }
}

} // namespace serializer
} // namespace rybench

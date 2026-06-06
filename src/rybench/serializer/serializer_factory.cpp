#include <rybench/serializer/serializer_factory.hpp>
#ifdef RYBENCH_ENABLE_JSON
#include "json/json_serializer.hpp"
#endif
#ifdef RYBENCH_ENABLE_PROTOBUF
#include "protobuf/protobuf_serializer.hpp"
#endif
#ifdef RYBENCH_ENABLE_FLATBUFFERS
#include "flatbuffers/flatbuffers_serializer.hpp"
#endif

namespace rybench::serializer {

std::expected<SerializerPtr, std::string> createSerializer(const core::Config& config,
                                                            std::string_view type) {
    try {
        std::string bfbsPath = config.get("schema", "bfbs", "");

#ifdef RYBENCH_ENABLE_JSON
        if (type == "json") {
            return std::make_unique<JsonSerializer>(bfbsPath);
        }
#endif
#ifdef RYBENCH_ENABLE_PROTOBUF
        if (type == "protobuf") {
            return std::make_unique<ProtobufSerializer>(bfbsPath);
        }
#endif
#ifdef RYBENCH_ENABLE_FLATBUFFERS
        if (type == "flatbuffers") {
            return std::make_unique<FlatbuffersSerializer>(bfbsPath);
        }
#endif
        return std::unexpected("Unknown or disabled serializer type: " + std::string(type));
    } catch (const std::exception& e) {
        return std::unexpected(std::string(e.what()));
    }
}

} // namespace rybench::serializer

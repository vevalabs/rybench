#include "flatbuffers_serializer.hpp"

namespace rybench {
namespace serializer {

std::string FlatbuffersSerializer::name() const {
    return "flatbuffers";
}

std::vector<uint8_t> FlatbuffersSerializer::serialize(const common::Message& msg) {
    // Already in FlatBuffers format, return as-is
    return msg.buffer();
}

} // namespace serializer
} // namespace rybench

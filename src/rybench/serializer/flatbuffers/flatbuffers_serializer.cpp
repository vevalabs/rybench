#include "flatbuffers_serializer.hpp"

namespace rybench {
namespace serializer {

std::string FlatbuffersSerializer::name() const {
    return "flatbuffers";
}

std::span<const uint8_t> FlatbuffersSerializer::serialize(const common::Message& msg) {
    buffer_ = msg.buffer();
    return buffer_;
}

} // namespace serializer
} // namespace rybench

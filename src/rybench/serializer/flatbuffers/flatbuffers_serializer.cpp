#include "flatbuffers_serializer.hpp"

namespace rybench::serializer {

std::string FlatbuffersSerializer::name() const {
    return "flatbuffers";
}

std::span<const uint8_t> FlatbuffersSerializer::serialize(const core::Message& msg) {
    buffer_ = msg.buffer();
    return buffer_;
}

} // namespace rybench::serializer

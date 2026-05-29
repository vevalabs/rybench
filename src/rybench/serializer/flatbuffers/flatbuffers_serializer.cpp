#include "flatbuffers_serializer.hpp"
#include <stdexcept>

namespace rybench::serializer {

std::string FlatbuffersSerializer::name() const {
    return "flatbuffers";
}

core::MessageFormat FlatbuffersSerializer::format() const {
    return core::MessageFormat::FlatBuffers;
}

std::span<const uint8_t> FlatbuffersSerializer::serialize(const core::Message& msg) {
    if (msg.format() == core::MessageFormat::FlatBuffers) {
        return msg.bytes();
    }
    throw std::runtime_error(
        std::string("Unsupported conversion: ")
        + std::string(core::toString(msg.format()))
        + " -> flatbuffers");
}

} // namespace rybench::serializer

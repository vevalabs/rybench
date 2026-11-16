#pragma once

#include <string_view>

namespace rybench::core {

enum class MessageFormat { FlatBuffers, Json, Protobuf, Monpoly, Unknown };

constexpr std::string_view toString(MessageFormat f) {
    switch (f) {
        case MessageFormat::FlatBuffers: return "flatbuffers";
        case MessageFormat::Json:        return "json";
        case MessageFormat::Protobuf:    return "protobuf";
        case MessageFormat::Monpoly:     return "monpoly";
        default:                         return "unknown";
    }
}

inline MessageFormat messageFormatFromString(std::string_view s) {
    if (s == "flatbuffers") return MessageFormat::FlatBuffers;
    if (s == "json")        return MessageFormat::Json;
    if (s == "protobuf")    return MessageFormat::Protobuf;
    if (s == "monpoly")     return MessageFormat::Monpoly;
    return MessageFormat::Unknown;
}

} // namespace rybench::core

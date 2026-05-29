#pragma once

#include <string_view>

namespace rybench::core {

enum class MessageFormat { FlatBuffers, Json, Protobuf, Unknown };

constexpr std::string_view toString(MessageFormat f) {
    switch (f) {
        case MessageFormat::FlatBuffers: return "flatbuffers";
        case MessageFormat::Json:        return "json";
        case MessageFormat::Protobuf:    return "protobuf";
        default:                         return "unknown";
    }
}

inline MessageFormat messageFormatFromString(std::string_view s) {
    if (s == "flatbuffers") return MessageFormat::FlatBuffers;
    if (s == "json")        return MessageFormat::Json;
    if (s == "protobuf")    return MessageFormat::Protobuf;
    return MessageFormat::Unknown;
}

} // namespace rybench::core

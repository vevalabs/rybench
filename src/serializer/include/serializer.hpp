#pragma once

#include "message.hpp"
#include <vector>
#include <cstdint>
#include <memory>
#include <string>

namespace serializer {

class ISerializer {
public:
    virtual ~ISerializer() = default;

    // Serialize Message (FlatBuffer) to target format
    // For flatbuffers: returns data as-is
    // For json: converts to JSON
    // For protobuf: converts to protobuf
    virtual std::vector<uint8_t> serialize(const common::Message& msg) = 0;

    virtual std::string name() const = 0;
};

using SerializerPtr = std::unique_ptr<ISerializer>;

} // namespace serializer

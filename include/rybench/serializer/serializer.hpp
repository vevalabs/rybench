#pragma once

#include <rybench/core/message.hpp>
#include <span>
#include <cstdint>
#include <memory>
#include <string>

namespace rybench::serializer {

class Serializer {
public:
    virtual ~Serializer() = default;

    virtual std::span<const uint8_t> serialize(const core::Message& msg) = 0;
    virtual std::string name() const = 0;
};

using SerializerPtr = std::unique_ptr<Serializer>;

} // namespace rybench::serializer

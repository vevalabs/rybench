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

    virtual std::string name() const = 0;
    virtual core::MessageFormat format() const = 0;
    virtual std::span<const uint8_t> serialize(const core::Message& msg) = 0;
};

using SerializerPtr = std::unique_ptr<Serializer>;

} // namespace rybench::serializer

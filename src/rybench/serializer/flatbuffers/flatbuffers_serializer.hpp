#pragma once

#include <rybench/serializer/serializer.hpp>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/reflection.h>
#include <string>

namespace rybench::serializer {

class FlatbuffersSerializer : public Serializer {
public:
    FlatbuffersSerializer() = default;

    std::string name() const override;
    core::MessageFormat format() const override;
    std::span<const uint8_t> serialize(const core::Message& msg) override;

};

} // namespace rybench::serializer

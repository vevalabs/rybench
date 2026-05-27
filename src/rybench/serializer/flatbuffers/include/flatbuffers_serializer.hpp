#pragma once

#include <rybench/serializer/serializer.hpp>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/reflection.h>
#include <string>

namespace rybench {
namespace serializer {

class FlatbuffersSerializer : public Serializer {
public:
    FlatbuffersSerializer() = default;

    std::span<const uint8_t> serialize(const common::Message& msg) override;
    std::string name() const override;

private:
    std::vector<uint8_t> buffer_;
};

} // namespace serializer
} // namespace rybench

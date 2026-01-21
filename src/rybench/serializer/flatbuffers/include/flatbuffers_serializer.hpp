#pragma once

#include "serializer.hpp"
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/reflection.h>
#include <string>

namespace rybench {
namespace serializer {

class FlatbuffersSerializer : public ISerializer {
public:
    FlatbuffersSerializer() = default;

    std::vector<uint8_t> serialize(const common::Message& msg) override;
    std::string name() const override;
};

} // namespace serializer
} // namespace rybench

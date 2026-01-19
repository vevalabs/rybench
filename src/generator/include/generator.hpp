#pragma once

#include "schema.hpp"
#include "flatbuffers_schema.hpp"
#include "message.hpp"
#include "data_value.hpp"
#include <memory>
#include <string>

namespace rybench {
namespace generator {

enum class Algorithm {
    Fixed,
    SemiRandom,
    SchemaGuided,
    PropertyGuided
};

class IGenerator {
public:
    virtual ~IGenerator() = default;

    // New FlatBuffers-based interface
    virtual void loadSchema(const common::FlatBuffersSchema& schema) = 0;
    virtual common::Message generate() = 0;

    virtual Algorithm algorithm() const = 0;
    virtual std::string name() const = 0;
};

using GeneratorPtr = std::unique_ptr<IGenerator>;

} // namespace generator
} // namespace rybench

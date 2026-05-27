#pragma once

#include <rybench/common/flatbuffers_schema.hpp>
#include <rybench/common/message.hpp>
#include <memory>
#include <string>

namespace rybench::generator {

class Generator {
public:
    virtual ~Generator() = default;

    virtual void loadSchema(const common::FlatBuffersSchema& schema) = 0;
    virtual common::Message generate() = 0;
    virtual std::string name() const = 0;
};

using GeneratorPtr = std::unique_ptr<Generator>;

} // namespace rybench::generator

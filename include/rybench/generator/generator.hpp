#pragma once

#include <rybench/core/flatbuffers_schema.hpp>
#include <rybench/core/message.hpp>
#include <memory>
#include <string>

namespace rybench::generator {

class Generator {
public:
    virtual ~Generator() = default;

    virtual void loadSchema(const core::FlatBuffersSchema& schema) = 0;
    virtual core::Message generate() = 0;
    virtual std::string name() const = 0;
};

using GeneratorPtr = std::unique_ptr<Generator>;

} // namespace rybench::generator

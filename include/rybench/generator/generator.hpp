#pragma once

#include <rybench/core/schema.hpp>
#include <rybench/core/message.hpp>
#include <memory>
#include <string>

namespace rybench::generator {

class Generator {
public:
    virtual ~Generator() = default;
    
    virtual std::string name() const = 0;

    virtual void loadSchema(const core::SchemaDefinition& schema) = 0;
    virtual core::Message generate() = 0;
    
};

using GeneratorPtr = std::unique_ptr<Generator>;

} // namespace rybench::generator

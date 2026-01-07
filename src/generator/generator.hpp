#pragma once

#include "common/schema.hpp"
#include "common/data_value.hpp"
#include <memory>
#include <string>

namespace generator {

enum class Algorithm {
    Fixed,          // Same data every time
    SemiRandom,     // Randomness with seed for reproducibility
    SchemaGuided,   // Random based on schema constraints
    PropertyGuided  // Guided by custom properties/rules
};

class IGenerator {
public:
    virtual ~IGenerator() = default;

    virtual void loadSchema(const common::Schema& schema) = 0;
    virtual common::DataValue generate() = 0;
    virtual Algorithm algorithm() const = 0;
    virtual std::string name() const = 0;
};

using GeneratorPtr = std::unique_ptr<IGenerator>;

} // namespace generator

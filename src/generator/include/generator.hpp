#pragma once

#include "schema.hpp"
#include "data_value.hpp"
#include <memory>
#include <string>

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

    virtual void loadSchema(const common::Schema& schema) = 0;
    virtual common::DataValue generate() = 0;
    virtual Algorithm algorithm() const = 0;
    virtual std::string name() const = 0;
};

using GeneratorPtr = std::unique_ptr<IGenerator>;

} // namespace generator

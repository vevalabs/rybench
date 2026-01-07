#pragma once

#include "common/data_value.hpp"
#include "common/schema.hpp"
#include <vector>
#include <cstdint>
#include <memory>

namespace serializer {

class ISerializer {
public:
    virtual ~ISerializer() = default;

    // Serialize DataValue to binary format
    virtual std::vector<uint8_t> serialize(const common::DataValue& value) = 0;

    // Deserialize binary to DataValue
    virtual common::DataValue deserialize(const std::vector<uint8_t>& data) = 0;

    // Get schema describing expected data structure for generator
    // Returns the schema that generator should use to produce DataValue
    virtual common::Schema getSchema() const = 0;

    virtual std::string name() const = 0;
};

using SerializerPtr = std::unique_ptr<ISerializer>;

} // namespace serializer

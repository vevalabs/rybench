#pragma once

#include "generator.hpp"
#include <random>
#include <cstdint>

namespace generator {

class SchemaGuidedGenerator : public IGenerator {
public:
    explicit SchemaGuidedGenerator(uint64_t seed = 0);

    void loadSchema(const common::Schema& schema) override;
    common::DataValue generate() override;
    Algorithm algorithm() const override;
    std::string name() const override;

private:
    common::Schema schema_;
    std::mt19937_64 rng_;

    common::DataValue generateField(const common::SchemaField& field);
    common::DataValue generateInt(const common::FieldConstraints& c);
    common::DataValue generateDouble(const common::FieldConstraints& c);
    common::DataValue generateString(const common::FieldConstraints& c);
    common::DataValue generateBool();
    common::DataValue generateArray(const common::SchemaField& field);
    common::DataValue generateObject(const common::SchemaField& field);
};

using RandomGenerator = SchemaGuidedGenerator;

} // namespace generator

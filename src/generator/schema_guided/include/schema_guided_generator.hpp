#pragma once

#include "generator.hpp"
#include "reflection_builder.hpp"
#include <random>
#include <cstdint>
#include <memory>

namespace rybench {
namespace generator {

class SchemaGuidedGenerator : public IGenerator {
public:
    explicit SchemaGuidedGenerator(uint64_t seed = 0);

    void loadSchema(const common::FlatBuffersSchema& schema) override;
    common::Message generate() override;
    Algorithm algorithm() const override;
    std::string name() const override;

private:
    const common::FlatBuffersSchema* schema_ = nullptr;
    std::mt19937_64 rng_;
    std::unique_ptr<ReflectionBuilder> builder_;
};

using RandomGenerator = SchemaGuidedGenerator;

} // namespace generator
} // namespace rybench

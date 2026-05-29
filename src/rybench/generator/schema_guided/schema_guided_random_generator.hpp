#pragma once

#include <rybench/generator/generator.hpp>
#include "reflection_builder.hpp"
#include <rybench/serializer/serializer.hpp>
#include <random>
#include <cstdint>
#include <memory>

namespace rybench::generator {

class SchemaGuidedRandomGenerator : public Generator {
public:
    explicit SchemaGuidedRandomGenerator(serializer::Serializer& backend, uint64_t seed = 0);

    void loadSchema(const core::FlatBuffersSchema& schema) override;
    core::Message generate() override;
    std::string name() const override;

private:
    serializer::Serializer& backend_;
    const core::FlatBuffersSchema* schema_ = nullptr;
    std::mt19937_64 rng_;
    std::unique_ptr<ReflectionBuilder> builder_;
};

} // namespace rybench::generator

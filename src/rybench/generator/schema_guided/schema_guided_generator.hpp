#pragma once

#include <rybench/generator/generator.hpp>
#include <rybench/generator/reflection_builder.hpp>
#include <random>
#include <cstdint>
#include <memory>

namespace rybench::generator {

class SchemaGuidedGenerator : public Generator {
public:
    explicit SchemaGuidedGenerator(uint64_t seed = 0);

    void loadSchema(const core::FlatBuffersSchema& schema) override;
    core::Message generate() override;
    std::string name() const override;

private:
    const core::FlatBuffersSchema* schema_ = nullptr;
    std::mt19937_64 rng_;
    std::unique_ptr<ReflectionBuilder> builder_;
};

using RandomGenerator = SchemaGuidedGenerator;

} // namespace rybench::generator

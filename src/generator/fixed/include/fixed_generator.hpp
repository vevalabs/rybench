#pragma once

#include "generator.hpp"
#include "reflection_builder.hpp"
#include <fstream>
#include <string>
#include <memory>

namespace generator {

class FixedGenerator : public IGenerator {
public:
    FixedGenerator() = default;
    ~FixedGenerator();

    void loadSchema(const common::FlatBuffersSchema& schema) override;
    void loadDump(const std::string& filepath);
    common::Message generate() override;

    void reset();
    bool finished() const;

    Algorithm algorithm() const override;
    std::string name() const override;

private:
    const common::FlatBuffersSchema* schema_ = nullptr;
    std::ifstream file_;
    bool finished_ = false;
    bool useSchemaData_ = false;
    std::unique_ptr<ReflectionBuilder> builder_;
    std::mt19937_64 rng_;  // For fixed seed generation

    common::Message buildFixedFromSchema();
};

} // namespace generator

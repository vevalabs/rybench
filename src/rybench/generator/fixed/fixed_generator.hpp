#pragma once

#include <rybench/generator/generator.hpp>
#include <rybench/serializer/serializer.hpp>
#include <fstream>
#include <string>

namespace rybench::generator {

class FixedGenerator : public Generator {
public:
    explicit FixedGenerator(serializer::Serializer& backend);
    ~FixedGenerator();

    void loadSchema(const core::SchemaDefinition& schema) override;
    void loadDump(const std::string& filepath);
    core::Message generate() override;
    std::string name() const override;

private:
    serializer::Serializer& backend_;
    const core::SchemaDefinition* schema_ = nullptr;
    std::ifstream file_;
    bool finished_ = false;
    core::Message lastMessage_;  // Cached for indefinite replay
};

} // namespace rybench::generator

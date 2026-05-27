#pragma once

#include <rybench/generator/generator.hpp>
#include <fstream>
#include <string>

namespace rybench::generator {

class FixedGenerator : public Generator {
public:
    FixedGenerator() = default;
    ~FixedGenerator();

    void loadSchema(const core::FlatBuffersSchema& schema) override;
    void loadDump(const std::string& filepath);
    core::Message generate() override;
    std::string name() const override;

private:
    const core::FlatBuffersSchema* schema_ = nullptr;
    std::ifstream file_;
    bool finished_ = false;
    core::Message lastMessage_;  // Cached for indefinite replay
};

} // namespace rybench::generator

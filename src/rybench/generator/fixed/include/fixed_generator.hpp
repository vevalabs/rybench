#pragma once

#include "generator.hpp"
#include <fstream>
#include <string>

namespace rybench {
namespace generator {

class FixedGenerator : public IGenerator {
public:
    FixedGenerator() = default;
    ~FixedGenerator();

    void loadSchema(const common::FlatBuffersSchema& schema) override;
    void loadDump(const std::string& filepath);
    common::Message generate() override;
    Algorithm algorithm() const override;
    std::string name() const override;

private:
    const common::FlatBuffersSchema* schema_ = nullptr;
    std::ifstream file_;
    bool finished_ = false;
    common::Message lastMessage_;  // Cached for indefinite replay
};

} // namespace generator
} // namespace rybench

#pragma once

#include <rybench/generator/generator.hpp>
#include <rybench/serializer/serializer.hpp>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

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
    // The whole dump is preloaded once; generate() cycles through the framed
    // messages and returns a zero-copy view into data_ (no per-message read/alloc).
    std::vector<uint8_t> data_;
    std::vector<std::pair<std::size_t, std::uint32_t>> frames_;  // (offset, size) per message
    std::size_t idx_ = 0;
};

} // namespace rybench::generator

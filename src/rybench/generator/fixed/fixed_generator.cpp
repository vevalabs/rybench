#include "fixed_generator.hpp"
#include <cstring>
#include <fstream>
#include <span>
#include <stdexcept>

namespace rybench::generator {

FixedGenerator::FixedGenerator(serializer::Serializer& backend)
    : backend_(backend) {}

FixedGenerator::~FixedGenerator() = default;

void FixedGenerator::loadSchema(const core::SchemaDefinition& schema) {
    schema_ = &schema;
    if (data_.empty())
        throw std::runtime_error("FixedGenerator: no dump loaded. Call loadDump() before loadSchema().");
}

void FixedGenerator::loadDump(const std::string& filepath) {
    // Preload the whole dump into memory once, then index the framed messages.
    std::ifstream f(filepath, std::ios::binary | std::ios::in | std::ios::ate);
    if (!f)
        throw std::runtime_error("Failed to open dump file: " + filepath);
    const std::streamsize sz = f.tellg();
    f.seekg(0);
    data_.resize(sz > 0 ? static_cast<std::size_t>(sz) : 0);
    if (sz > 0 && !f.read(reinterpret_cast<char*>(data_.data()), sz))
        throw std::runtime_error("Failed to read dump file: " + filepath);

    // Frame layout: 4-byte little-endian size + that many payload bytes, repeated.
    frames_.clear();
    std::size_t off = 0;
    while (off + sizeof(std::uint32_t) <= data_.size()) {
        std::uint32_t msgSize = 0;
        std::memcpy(&msgSize, data_.data() + off, sizeof(msgSize));
        off += sizeof(msgSize);
        if (off + msgSize > data_.size()) break;   // truncated tail
        frames_.emplace_back(off, msgSize);
        off += msgSize;
    }
    idx_ = 0;
}

core::Message FixedGenerator::generate() {
    if (frames_.empty())
        return core::Message();
    const auto [off, sz] = frames_[idx_];
    idx_ = (idx_ + 1 < frames_.size()) ? idx_ + 1 : 0;   // cycle through the dump
    // Zero-copy: a non-owning view into the preloaded buffer (no read, no alloc).
    return core::Message(std::span<const uint8_t>(data_.data() + off, sz), backend_.format());
}

std::string FixedGenerator::name() const {
    return "fixed";
}

} // namespace rybench::generator

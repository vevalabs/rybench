#include "fixed_generator.hpp"
#include <stdexcept>

namespace rybench::generator {

FixedGenerator::FixedGenerator(serializer::Serializer& backend)
    : backend_(backend) {}

FixedGenerator::~FixedGenerator() {
    if (file_.is_open()) {
        file_.close();
    }
}

void FixedGenerator::loadSchema(const core::FlatBuffersSchema& schema) {
    schema_ = &schema;

    if (!file_.is_open()) {
        throw std::runtime_error("FixedGenerator: no dump file loaded. Call loadDump() before loadSchema().");
    }
}

void FixedGenerator::loadDump(const std::string& filepath) {
    file_.open(filepath, std::ios::binary | std::ios::in);
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to open dump file: " + filepath);
    }
    finished_ = false;
}

core::Message FixedGenerator::generate() {
    if (finished_) {
        return lastMessage_;
    }

    // Format: 4 bytes size (little-endian) + size bytes data
    uint32_t msgSize = 0;
    if (!file_.read(reinterpret_cast<char*>(&msgSize), sizeof(msgSize))) {
        finished_ = true;
        return lastMessage_;
    }

    std::vector<uint8_t> buffer(msgSize);
    if (!file_.read(reinterpret_cast<char*>(buffer.data()), msgSize)) {
        finished_ = true;
        return lastMessage_;
    }

    // TODO: preload replay dump once, store message offsets, and return borrowed spans
    // into the preloaded buffer for zero-copy replay.
    lastMessage_ = core::Message(std::move(buffer), backend_.format(),
                                  schema_ ? schema_->reflectionSchema() : nullptr);
    return lastMessage_;
}

std::string FixedGenerator::name() const {
    return "fixed";
}

} // namespace rybench::generator

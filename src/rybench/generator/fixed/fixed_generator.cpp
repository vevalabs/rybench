#include "include/fixed_generator.hpp"
#include <stdexcept>

namespace rybench {
namespace generator {

FixedGenerator::~FixedGenerator() {
    if (file_.is_open()) {
        file_.close();
    }
}

void FixedGenerator::loadSchema(const common::FlatBuffersSchema& schema) {
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

common::Message FixedGenerator::generate() {
    // If we've reached EOF, repeat the last message indefinitely
    if (finished_) {
        return lastMessage_;
    }

    // Read binary FlatBuffers message from file
    // Format: 4 bytes size (little-endian) + size bytes data
    uint32_t msgSize = 0;
    if (!file_.read(reinterpret_cast<char*>(&msgSize), sizeof(msgSize))) {
        finished_ = true;
        // Return last cached message (repeats indefinitely)
        return lastMessage_;
    }

    std::vector<uint8_t> buffer(msgSize);
    if (!file_.read(reinterpret_cast<char*>(buffer.data()), msgSize)) {
        finished_ = true;
        // Return last cached message (repeats indefinitely)
        return lastMessage_;
    }

    lastMessage_ = common::Message(std::move(buffer), schema_ ? schema_->reflectionSchema() : nullptr);
    return lastMessage_;
}

Algorithm FixedGenerator::algorithm() const {
    return Algorithm::Fixed;
}

std::string FixedGenerator::name() const {
    return "fixed";
}

} // namespace generator
} // namespace rybench

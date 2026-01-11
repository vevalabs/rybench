#include "include/fixed_generator.hpp"
#include <stdexcept>

namespace generator {

FixedGenerator::~FixedGenerator() {
    if (file_.is_open()) {
        file_.close();
    }
}

void FixedGenerator::loadSchema(const common::FlatBuffersSchema& schema) {
    schema_ = &schema;
    // Use fixed seed for deterministic output
    rng_.seed(42);
    builder_ = std::make_unique<ReflectionBuilder>(schema);

    if (!file_.is_open()) {
        useSchemaData_ = true;
    }
}

void FixedGenerator::loadDump(const std::string& filepath) {
    file_.open(filepath, std::ios::binary | std::ios::in);
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to open dump file: " + filepath);
    }
    useSchemaData_ = false;
    finished_ = false;
}

common::Message FixedGenerator::generate() {
    if (finished_) {
        return common::Message();
    }

    if (useSchemaData_) {
        return buildFixedFromSchema();
    }

    // Read binary FlatBuffers message from file
    // Format: 4 bytes size (little-endian) + size bytes data
    uint32_t msgSize = 0;
    if (!file_.read(reinterpret_cast<char*>(&msgSize), sizeof(msgSize))) {
        finished_ = true;
        return common::Message();
    }

    std::vector<uint8_t> buffer(msgSize);
    if (!file_.read(reinterpret_cast<char*>(buffer.data()), msgSize)) {
        finished_ = true;
        return common::Message();
    }

    return common::Message(std::move(buffer), schema_ ? schema_->reflectionSchema() : nullptr);
}

void FixedGenerator::reset() {
    if (file_.is_open()) {
        file_.clear();
        file_.seekg(0, std::ios::beg);
    }
    finished_ = false;
    // Reset RNG for deterministic replay
    rng_.seed(42);
}

bool FixedGenerator::finished() const {
    return finished_;
}

Algorithm FixedGenerator::algorithm() const {
    return Algorithm::Fixed;
}

std::string FixedGenerator::name() const {
    return "fixed";
}

common::Message FixedGenerator::buildFixedFromSchema() {
    if (!builder_) {
        return common::Message();
    }
    return builder_->build(rng_);
}

} // namespace generator

#include "schema_guided_random_generator.hpp"
#include <chrono>

namespace rybench::generator {

SchemaGuidedRandomGenerator::SchemaGuidedRandomGenerator(serializer::Serializer& backend, uint64_t seed)
    : backend_(backend) {
    if (backend_.format() != core::MessageFormat::FlatBuffers) {
        throw std::runtime_error(
            "schema_guided generator currently supports only flatbuffers backend, got: "
            + std::string(core::toString(backend_.format())));
    }
    if (seed == 0) {
        rng_.seed(std::chrono::steady_clock::now().time_since_epoch().count());
    } else {
        rng_.seed(seed);
    }
}

void SchemaGuidedRandomGenerator::loadSchema(const core::FlatBuffersSchema& schema) {
    schema_ = &schema;
    builder_ = std::make_unique<ReflectionBuilder>(schema);
}

core::Message SchemaGuidedRandomGenerator::generate() {
    if (!builder_) {
        return core::Message();
    }
    return builder_->build(rng_);
}

std::string SchemaGuidedRandomGenerator::name() const {
    return "schema_guided";
}

} // namespace rybench::generator

#include "schema_guided_generator.hpp"
#include <chrono>

namespace rybench::generator {

SchemaGuidedGenerator::SchemaGuidedGenerator(uint64_t seed) {
    if (seed == 0) {
        rng_.seed(std::chrono::steady_clock::now().time_since_epoch().count());
    } else {
        rng_.seed(seed);
    }
}

void SchemaGuidedGenerator::loadSchema(const core::FlatBuffersSchema& schema) {
    schema_ = &schema;
    builder_ = std::make_unique<ReflectionBuilder>(schema);
}

core::Message SchemaGuidedGenerator::generate() {
    if (!builder_) {
        return core::Message();
    }
    return builder_->build(rng_);
}

std::string SchemaGuidedGenerator::name() const {
    return "schema_guided";
}

} // namespace rybench::generator

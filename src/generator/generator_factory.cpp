#include "generator_factory.hpp"
#include "fixed_generator.hpp"
#include "schema_guided_generator.hpp"
#include "property_guided_generator.hpp"

#include <iostream>

namespace generator {

GeneratorPtr createGenerator(const common::Config& config, const common::Schema& schema) {
    std::string algorithm = config.get("generator", "algorithm", "schema_guided");
    uint64_t seed = config.getInt("generator", "seed", 0);

    if (algorithm == "fixed") {
        auto gen = std::make_unique<FixedGenerator>();
        std::string replayFile = config.get("generator", "replay", "");
        if (!replayFile.empty()) {
            try {
                gen->loadDump(replayFile);
                std::cout << "Replay from: " << replayFile << std::endl;
            } catch (const std::exception& e) {
                std::cerr << "Warning: Failed to load replay file: " << e.what() << std::endl;
                std::cerr << "Using schema-based fixed data instead" << std::endl;
            }
        }
        gen->loadSchema(schema);
        return gen;
    }

    if (algorithm == "property_guided") {
        auto gen = std::make_unique<PropertyGuidedGenerator>();
        gen->loadSchema(schema);
        gen->setTimestamp("timestamp");
        gen->setCounter("sequence", 0);
        return gen;
    }

    auto gen = std::make_unique<SchemaGuidedGenerator>(seed);
    gen->loadSchema(schema);
    if (seed != 0) {
        std::cout << "Seed: " << seed << std::endl;
    }
    return gen;
}

} // namespace generator

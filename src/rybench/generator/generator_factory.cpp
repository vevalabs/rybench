#include <rybench/generator/generator_factory.hpp>
#include "fixed/fixed_generator.hpp"
#include "schema_guided/schema_guided_random_generator.hpp"
#include "property_guided/property_guided_mtl_generator.hpp"

#include <iostream>

namespace rybench::generator {

std::expected<GeneratorPtr, std::string> createGenerator(const core::Config& config,
                                                          serializer::Serializer& generationSerializer) {
    try {
        std::string algorithm = config.get("generator", "algorithm", "schema_guided");
        uint64_t seed = config.getInt("generator", "seed", 0);
        const core::SchemaDefinition* schema = generationSerializer.loadedSchema();

        if (algorithm == "fixed") {
            auto gen = std::make_unique<FixedGenerator>(generationSerializer);
            std::string replayFile = config.get("generator", "replay", "");
            if (!replayFile.empty()) {
                gen->loadDump(replayFile);
                std::cout << "Replay from: " << replayFile << "\n";
            }
            if (schema) gen->loadSchema(*schema);
            return gen;
        }

        if (algorithm == "property_guided") {
            double timeStep = 1.0;
            auto gen = std::make_unique<PropertyGuidedMTLGenerator>(generationSerializer, timeStep, seed);
            if (schema) gen->loadSchema(*schema);   // parses formula + sets up the backend
            return gen;
        }

        auto gen = std::make_unique<SchemaGuidedRandomGenerator>(generationSerializer, seed);
        if (schema) gen->loadSchema(*schema);
        if (seed != 0) {
            std::cout << "Seed: " << seed << "\n";
        }
        return gen;
    } catch (const std::exception& e) {
        return std::unexpected(std::string(e.what()));
    }
}

} // namespace rybench::generator

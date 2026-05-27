#include <rybench/generator/generator_factory.hpp>
#include "fixed/fixed_generator.hpp"
#include "schema_guided/schema_guided_generator.hpp"
#include "property_guided/property_guided_generator.hpp"

#include <iostream>

namespace rybench::generator {

std::expected<GeneratorPtr, std::string> createGenerator(const common::Config& config) {
    try {
        const auto& schema = config.schema();
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
    } catch (const std::exception& e) {
        return std::unexpected(std::string(e.what()));
    }
}

} // namespace rybench::generator

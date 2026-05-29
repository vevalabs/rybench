#include <rybench/generator/generator_factory.hpp>
#include "fixed/fixed_generator.hpp"
#include "schema_guided/schema_guided_random_generator.hpp"

#include <iostream>

namespace rybench::generator {

std::expected<GeneratorPtr, std::string> createGenerator(const core::Config& config,
                                                          serializer::Serializer& generationSerializer) {
    try {
        const auto& schema = config.schema();
        std::string algorithm = config.get("generator", "algorithm", "schema_guided");
        uint64_t seed = config.getInt("generator", "seed", 0);

        if (algorithm == "fixed") {
            auto gen = std::make_unique<FixedGenerator>(generationSerializer);
            std::string replayFile = config.get("generator", "replay", "");
            if (!replayFile.empty()) {
                gen->loadDump(replayFile);
                std::cout << "Replay from: " << replayFile << "\n";
            }
            gen->loadSchema(schema);
            return gen;
        }

        auto gen = std::make_unique<SchemaGuidedRandomGenerator>(generationSerializer, seed);
        gen->loadSchema(schema);
        if (seed != 0) {
            std::cout << "Seed: " << seed << "\n";
        }
        return gen;
    } catch (const std::exception& e) {
        return std::unexpected(std::string(e.what()));
    }
}

} // namespace rybench::generator

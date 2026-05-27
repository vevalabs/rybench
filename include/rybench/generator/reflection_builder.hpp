#pragma once

#include <rybench/common/flatbuffers_schema.hpp>
#include <rybench/common/message.hpp>
#include <flatbuffers/flatbuffers.h>
#include <flatbuffers/reflection.h>
#include <random>
#include <functional>
#include <unordered_map>
#include <regex>

namespace rybench {
namespace generator {

class ReflectionBuilder {
public:
    explicit ReflectionBuilder(const common::FlatBuffersSchema& schema);

    // Build a complete message with random values within constraints
    common::Message build(std::mt19937_64& rng);

    // Custom value generators for specific field paths (e.g., "timestamp", "sensors.id")
    using ScalarGenerator = std::function<int64_t(std::mt19937_64&)>;
    using DoubleGenerator = std::function<double(std::mt19937_64&)>;
    using StringGenerator = std::function<std::string(std::mt19937_64&)>;

    void setIntGenerator(const std::string& fieldPath, ScalarGenerator gen);
    void setDoubleGenerator(const std::string& fieldPath, DoubleGenerator gen);
    void setStringGenerator(const std::string& fieldPath, StringGenerator gen);

    // Convenience methods for common patterns
    void setCounter(const std::string& fieldPath, int64_t start = 0);
    void setTimestamp(const std::string& fieldPath);
    void setFixedValue(const std::string& fieldPath, int64_t value);
    void setFixedValue(const std::string& fieldPath, double value);
    void setFixedValue(const std::string& fieldPath, const std::string& value);

private:
    const common::FlatBuffersSchema& schema_;

    std::unordered_map<std::string, ScalarGenerator> intGenerators_;
    std::unordered_map<std::string, DoubleGenerator> doubleGenerators_;
    std::unordered_map<std::string, StringGenerator> stringGenerators_;
    std::unordered_map<std::string, int64_t> counters_;

    // Build methods
    flatbuffers::Offset<void> buildTable(
        flatbuffers::FlatBufferBuilder& builder,
        const reflection::Object* table,
        const std::vector<common::FbsSchemaField>& fields,
        std::mt19937_64& rng,
        const std::string& path = "");

    flatbuffers::Offset<flatbuffers::String> buildString(
        flatbuffers::FlatBufferBuilder& builder,
        const common::FbsFieldConstraints& constraints,
        std::mt19937_64& rng,
        const std::string& path);

    flatbuffers::Offset<void> buildVector(
        flatbuffers::FlatBufferBuilder& builder,
        const reflection::Field* field,
        const common::FbsSchemaField& schemaField,
        std::mt19937_64& rng,
        const std::string& path);

    flatbuffers::Offset<void> buildStruct(
        flatbuffers::FlatBufferBuilder& builder,
        const reflection::Object* structDef,
        const std::vector<common::FbsSchemaField>& fields,
        std::mt19937_64& rng,
        const std::string& path);

    // Scalar generation with constraints
    int64_t generateInt(const common::FbsFieldConstraints& c, std::mt19937_64& rng,
                        const std::string& path, bool isUnsigned = false);
    double generateDouble(const common::FbsFieldConstraints& c, std::mt19937_64& rng,
                         const std::string& path);
    bool generateBool(std::mt19937_64& rng);
    std::string generateString(const common::FbsFieldConstraints& c, std::mt19937_64& rng,
                               const std::string& path);
    std::string generatePatternString(const std::string& pattern, size_t minLen, size_t maxLen,
                                      std::mt19937_64& rng);

    // Helpers
    std::string fieldPath(const std::string& basePath, const std::string& fieldName) const;
};

} // namespace generator
} // namespace rybench

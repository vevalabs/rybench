#pragma once

#include <rybench/generator/generator.hpp>
#include <rybench/serializer/serializer.hpp>
#include <random>
#include <cstdint>
#include <string>
#include <unordered_map>

namespace rybench::generator {

class SchemaGuidedRandomGenerator : public Generator {
public:
    explicit SchemaGuidedRandomGenerator(serializer::Serializer& backend, uint64_t seed = 0);

    void loadSchema(const core::SchemaDefinition& schema) override;
    core::Message generate() override;
    std::string name() const override;

private:
    serializer::Serializer& backend_;
    const std::vector<core::SchemaField>* fields_ = nullptr;
    std::mt19937_64 rng_;

    // Per-field counter state, keyed by stable SchemaField address. Only ever
    // touched for fields with FieldRole::Counter — ordinary fields pay nothing.
    std::unordered_map<const core::SchemaField*, uint64_t> counters_;

    void walkFields(const std::vector<core::SchemaField>& fields);

    // Resolves an integer field's value honouring its FieldRole (timestamp /
    // counter), falling back to a constrained random value for FieldRole::None.
    int64_t     resolveInt    (const core::SchemaField& sf, bool isUnsigned);
    static uint64_t nowMillis();

    int64_t     generateInt   (const core::FieldConstraints& c, bool isUnsigned = false);
    double      generateDouble(const core::FieldConstraints& c);
    bool        generateBool  ();
    std::string generateString(const core::FieldConstraints& c);
    std::string generatePatternString(const std::string& pattern, size_t minLen, size_t maxLen);
    int64_t     generateEnum  (const core::SchemaField& sf);
};

} // namespace rybench::generator

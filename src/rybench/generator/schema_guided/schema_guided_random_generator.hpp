#pragma once

#include <rybench/generator/generator.hpp>
#include <rybench/serializer/serializer.hpp>
#include <random>
#include <cstdint>
#include <string>

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

    void walkFields(const std::vector<core::SchemaField>& fields);

    int64_t     generateInt   (const core::FieldConstraints& c, bool isUnsigned = false);
    double      generateDouble(const core::FieldConstraints& c);
    bool        generateBool  ();
    std::string generateString(const core::FieldConstraints& c);
    std::string generatePatternString(const std::string& pattern, size_t minLen, size_t maxLen);
    int64_t     generateEnum  (const core::SchemaField& sf);
};

} // namespace rybench::generator

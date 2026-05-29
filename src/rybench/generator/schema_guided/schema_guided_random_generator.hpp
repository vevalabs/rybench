#pragma once

#include <rybench/generator/generator.hpp>
#include "reflection_builder.hpp"
#include <rybench/serializer/serializer.hpp>
#include <random>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <functional>

namespace rybench::generator {

class SchemaGuidedRandomGenerator : public Generator {
public:
    explicit SchemaGuidedRandomGenerator(serializer::Serializer& backend, uint64_t seed = 0);

    void loadSchema(const core::FlatBuffersSchema& schema) override;
    core::Message generate() override;
    std::string name() const override;

    using ScalarGenerator = std::function<int64_t(std::mt19937_64&)>;
    using DoubleGenerator = std::function<double(std::mt19937_64&)>;
    using StringGenerator = std::function<std::string(std::mt19937_64&)>;

    void setCounter   (const std::string& fieldPath, int64_t start = 0);
    void setTimestamp (const std::string& fieldPath);
    void setFixedValue(const std::string& fieldPath, int64_t value);
    void setFixedValue(const std::string& fieldPath, double value);
    void setFixedValue(const std::string& fieldPath, const std::string& value);

private:
    serializer::Serializer& backend_;
    const core::FlatBuffersSchema* schema_ = nullptr;
    std::mt19937_64 rng_;
    std::unique_ptr<ReflectionBuilder> builder_;

    std::unordered_map<std::string, ScalarGenerator> intGenerators_;
    std::unordered_map<std::string, DoubleGenerator> doubleGenerators_;
    std::unordered_map<std::string, StringGenerator> stringGenerators_;
    std::unordered_map<std::string, int64_t>         counters_;

    ReflectionBuilder::Samplers samplers_;

    void buildSamplers();

    int64_t     generateInt   (const core::FbsFieldConstraints& c, const std::string& path, bool isUnsigned = false);
    double      generateDouble(const core::FbsFieldConstraints& c, const std::string& path);
    bool        generateBool  ();
    std::string generateString(const core::FbsFieldConstraints& c, const std::string& path);
    std::string generatePatternString(const std::string& pattern, size_t minLen, size_t maxLen);
};

} // namespace rybench::generator

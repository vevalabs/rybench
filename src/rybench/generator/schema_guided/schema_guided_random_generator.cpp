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
    buildSamplers();
}

void SchemaGuidedRandomGenerator::buildSamplers() {
    samplers_.sampleInt = [this](const core::FbsFieldConstraints& c, const std::string& path, bool u) {
        return generateInt(c, path, u);
    };
    samplers_.sampleDouble = [this](const core::FbsFieldConstraints& c, const std::string& path) {
        return generateDouble(c, path);
    };
    samplers_.sampleString = [this](const core::FbsFieldConstraints& c, const std::string& path) {
        return generateString(c, path);
    };
    samplers_.sampleBool = [this]() {
        return generateBool();
    };
    samplers_.sampleEnum = [this](const reflection::Enum* enumDef) -> int64_t {
        if (!enumDef || !enumDef->values() || enumDef->values()->size() == 0) return 0;
        std::uniform_int_distribution<size_t> dist(0, enumDef->values()->size() - 1);
        return enumDef->values()->Get(dist(rng_))->value();
    };
    samplers_.sampleVectorSize = [this](size_t min, size_t max) -> size_t {
        std::uniform_int_distribution<size_t> dist(min, max);
        return dist(rng_);
    };
}

core::Message SchemaGuidedRandomGenerator::generate() {
    if (!builder_) return core::Message();
    return builder_->build(samplers_);
}

std::string SchemaGuidedRandomGenerator::name() const {
    return "schema_guided";
}

void SchemaGuidedRandomGenerator::setCounter(const std::string& fieldPath, int64_t start) {
    counters_[fieldPath] = start;
    intGenerators_[fieldPath] = [this, fieldPath](std::mt19937_64&) {
        return counters_[fieldPath]++;
    };
}

void SchemaGuidedRandomGenerator::setTimestamp(const std::string& fieldPath) {
    intGenerators_[fieldPath] = [](std::mt19937_64&) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
    };
}

void SchemaGuidedRandomGenerator::setFixedValue(const std::string& fieldPath, int64_t value) {
    intGenerators_[fieldPath] = [value](std::mt19937_64&) { return value; };
}

void SchemaGuidedRandomGenerator::setFixedValue(const std::string& fieldPath, double value) {
    doubleGenerators_[fieldPath] = [value](std::mt19937_64&) { return value; };
}

void SchemaGuidedRandomGenerator::setFixedValue(const std::string& fieldPath, const std::string& value) {
    stringGenerators_[fieldPath] = [value](std::mt19937_64&) { return value; };
}

int64_t SchemaGuidedRandomGenerator::generateInt(
    const core::FbsFieldConstraints& c, const std::string& path, bool isUnsigned) {

    auto it = intGenerators_.find(path);
    if (it != intGenerators_.end()) return it->second(rng_);

    int64_t minVal = static_cast<int64_t>(c.min.value_or(isUnsigned ? 0 : -1000));
    int64_t maxVal = static_cast<int64_t>(c.max.value_or(1000));
    if (isUnsigned && minVal < 0) minVal = 0;

    std::uniform_int_distribution<int64_t> dist(minVal, maxVal);
    return dist(rng_);
}

double SchemaGuidedRandomGenerator::generateDouble(
    const core::FbsFieldConstraints& c, const std::string& path) {

    auto it = doubleGenerators_.find(path);
    if (it != doubleGenerators_.end()) return it->second(rng_);

    double minVal = c.min.value_or(-100.0);
    double maxVal = c.max.value_or(100.0);
    std::uniform_real_distribution<double> dist(minVal, maxVal);
    return dist(rng_);
}

bool SchemaGuidedRandomGenerator::generateBool() {
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(rng_) == 1;
}

std::string SchemaGuidedRandomGenerator::generateString(
    const core::FbsFieldConstraints& c, const std::string& path) {

    auto it = stringGenerators_.find(path);
    if (it != stringGenerators_.end()) return it->second(rng_);

    size_t minLen = c.minLength.value_or(3);
    size_t maxLen = c.maxLength.value_or(12);

    if (c.pattern) return generatePatternString(*c.pattern, minLen, maxLen);

    std::uniform_int_distribution<size_t> lenDist(minLen, maxLen);
    size_t len = lenDist(rng_);

    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::uniform_int_distribution<size_t> charDist(0, sizeof(charset) - 2);

    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) result += charset[charDist(rng_)];
    return result;
}

std::string SchemaGuidedRandomGenerator::generatePatternString(
    const std::string& pattern, size_t minLen, size_t maxLen) {

    std::string charset;
    if (pattern == "^[A-Z]+$" || pattern.find("[A-Z]") != std::string::npos)
        charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    else if (pattern == "^[a-z]+$" || pattern.find("[a-z]") != std::string::npos)
        charset = "abcdefghijklmnopqrstuvwxyz";
    else if (pattern == "^[0-9]+$" || pattern.find("[0-9]") != std::string::npos)
        charset = "0123456789";
    else if (pattern.find("[A-Za-z]") != std::string::npos)
        charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    else
        charset = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

    std::uniform_int_distribution<size_t> lenDist(minLen, maxLen);
    size_t len = lenDist(rng_);
    std::uniform_int_distribution<size_t> charDist(0, charset.size() - 1);

    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) result += charset[charDist(rng_)];
    return result;
}

} // namespace rybench::generator

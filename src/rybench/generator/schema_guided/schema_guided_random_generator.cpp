#include "schema_guided_random_generator.hpp"
#include <chrono>

namespace rybench::generator {

SchemaGuidedRandomGenerator::SchemaGuidedRandomGenerator(serializer::Serializer& backend, uint64_t seed)
    : backend_(backend) {
    if (seed == 0)
        rng_.seed(std::chrono::steady_clock::now().time_since_epoch().count());
    else
        rng_.seed(seed);
}

void SchemaGuidedRandomGenerator::loadSchema(const core::SchemaDefinition& schema) {
    fields_ = &schema.fields();
    counters_.clear();
    backend_.loadSchema(schema);
}

core::Message SchemaGuidedRandomGenerator::generate() {
    if (!fields_) return core::Message();
    backend_.initMessage();
    walkFields(*fields_);
    return backend_.finalizeMessage();
}

std::string SchemaGuidedRandomGenerator::name() const { return "schema_guided"; }

void SchemaGuidedRandomGenerator::walkFields(const std::vector<core::SchemaField>& fields)
{
    for (const auto& sf : fields) {
        switch (sf.type) {
            case core::FieldType::Bool:
                backend_.addBool(sf, generateBool());
                break;
            case core::FieldType::Byte:
            case core::FieldType::Short:
            case core::FieldType::Int:
            case core::FieldType::Long:
                backend_.addInt(sf, resolveInt(sf, false));
                break;
            case core::FieldType::UByte:
            case core::FieldType::UShort:
            case core::FieldType::UInt:
            case core::FieldType::ULong:
                backend_.addUInt(sf, static_cast<uint64_t>(resolveInt(sf, true)));
                break;
            case core::FieldType::Float:
                backend_.addFloat(sf, static_cast<float>(generateDouble(sf.constraints)));
                break;
            case core::FieldType::Double:
                backend_.addDouble(sf, generateDouble(sf.constraints));
                break;
            case core::FieldType::String:
                backend_.addString(sf, generateString(sf.constraints));
                break;
            case core::FieldType::Enum:
                backend_.addInt(sf, generateEnum(sf));
                break;
            case core::FieldType::Table:
                backend_.beginTable(sf);
                walkFields(sf.children);
                backend_.endTable();
                break;
            case core::FieldType::Vector: {
                size_t minItems = sf.constraints.minItems.value_or(1);
                size_t maxItems = sf.constraints.maxItems.value_or(5);
                std::uniform_int_distribution<size_t> dist(minItems, maxItems);
                size_t count = dist(rng_);
                backend_.beginVector(sf, count);
                for (size_t i = 0; i < count; ++i)
                    walkFields(sf.children);
                backend_.endVector();
                break;
            }
            default:
                break;
        }
    }
}

int64_t SchemaGuidedRandomGenerator::generateEnum(const core::SchemaField& sf) {
    if (sf.enumNumericValues.empty()) return 0;
    std::uniform_int_distribution<size_t> dist(0, sf.enumNumericValues.size() - 1);
    return sf.enumNumericValues[dist(rng_)];
}

int64_t SchemaGuidedRandomGenerator::resolveInt(const core::SchemaField& sf, bool isUnsigned) {
    // Single predicted branch for the common (None) case; no allocation, no
    // string work. Counter/timestamp fields are the rare exception.
    switch (sf.role) {
        case core::FieldRole::Timestamp:
            return static_cast<int64_t>(nowMillis());
        case core::FieldRole::Counter:
            return static_cast<int64_t>(counters_[&sf]++);
        default:
            return generateInt(sf.constraints, isUnsigned);
    }
}

uint64_t SchemaGuidedRandomGenerator::nowMillis() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
}

int64_t SchemaGuidedRandomGenerator::generateInt(
    const core::FieldConstraints& c, bool isUnsigned)
{
    int64_t minVal = static_cast<int64_t>(c.min.value_or(isUnsigned ? 0 : -1000));
    int64_t maxVal = static_cast<int64_t>(c.max.value_or(1000));
    if (isUnsigned && minVal < 0) minVal = 0;

    std::uniform_int_distribution<int64_t> dist(minVal, maxVal);
    return dist(rng_);
}

double SchemaGuidedRandomGenerator::generateDouble(const core::FieldConstraints& c)
{
    double minVal = c.min.value_or(-100.0);
    double maxVal = c.max.value_or(100.0);
    std::uniform_real_distribution<double> dist(minVal, maxVal);
    return dist(rng_);
}

bool SchemaGuidedRandomGenerator::generateBool() {
    std::uniform_int_distribution<int> dist(0, 1);
    return dist(rng_) == 1;
}

std::string SchemaGuidedRandomGenerator::generateString(const core::FieldConstraints& c)
{
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
    const std::string& pattern, size_t minLen, size_t maxLen)
{
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

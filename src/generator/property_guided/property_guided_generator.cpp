#include "property_guided_generator.hpp"
#include <chrono>

namespace generator {

PropertyGuidedGenerator::PropertyGuidedGenerator()
    : rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {}

void PropertyGuidedGenerator::loadSchema(const common::Schema& schema) {
    schema_ = schema;
}

void PropertyGuidedGenerator::setProperty(const std::string& fieldName, std::function<common::DataValue()> gen) {
    properties_[fieldName] = gen;
}

void PropertyGuidedGenerator::setCounter(const std::string& fieldName, int64_t start) {
    int64_t counter = start;
    properties_[fieldName] = [counter]() mutable {
        return common::DataValue(counter++);
    };
}

void PropertyGuidedGenerator::setTimestamp(const std::string& fieldName) {
    properties_[fieldName] = []() {
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        return common::DataValue(static_cast<int64_t>(ms));
    };
}

common::DataValue PropertyGuidedGenerator::generate() {
    common::DataObject obj;
    for (const auto& field : schema_.fields()) {
        obj[field.name] = generateField(field);
    }
    return common::DataValue(std::move(obj));
}

Algorithm PropertyGuidedGenerator::algorithm() const {
    return Algorithm::PropertyGuided;
}

std::string PropertyGuidedGenerator::name() const {
    return "property_guided";
}

common::DataValue PropertyGuidedGenerator::generateField(const common::SchemaField& field) {
    auto it = properties_.find(field.name);
    if (it != properties_.end()) {
        return it->second();
    }

    switch (field.type) {
        case common::FieldType::Int:
            return generateInt(field.constraints);
        case common::FieldType::Double:
            return generateDouble(field.constraints);
        case common::FieldType::String:
            return generateString(field.constraints);
        case common::FieldType::Bool:
            return generateBool();
        case common::FieldType::Array:
            return generateArray(field);
        case common::FieldType::Object:
            return generateObject(field);
    }
    return common::DataValue(nullptr);
}

common::DataValue PropertyGuidedGenerator::generateInt(const common::FieldConstraints& c) {
    int64_t minVal = static_cast<int64_t>(c.min.value_or(0));
    int64_t maxVal = static_cast<int64_t>(c.max.value_or(1000));
    std::uniform_int_distribution<int64_t> dist(minVal, maxVal);
    return common::DataValue(dist(rng_));
}

common::DataValue PropertyGuidedGenerator::generateDouble(const common::FieldConstraints& c) {
    double minVal = c.min.value_or(0.0);
    double maxVal = c.max.value_or(1.0);
    std::uniform_real_distribution<double> dist(minVal, maxVal);
    return common::DataValue(dist(rng_));
}

common::DataValue PropertyGuidedGenerator::generateString(const common::FieldConstraints& c) {
    size_t minLen = c.minLength.value_or(5);
    size_t maxLen = c.maxLength.value_or(20);
    std::uniform_int_distribution<size_t> lenDist(minLen, maxLen);
    size_t len = lenDist(rng_);

    static const char charset[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::uniform_int_distribution<size_t> charDist(0, sizeof(charset) - 2);

    std::string result;
    result.reserve(len);
    for (size_t i = 0; i < len; ++i) {
        result += charset[charDist(rng_)];
    }
    return common::DataValue(std::move(result));
}

common::DataValue PropertyGuidedGenerator::generateBool() {
    std::uniform_int_distribution<int> dist(0, 1);
    return common::DataValue(dist(rng_) == 1);
}

common::DataValue PropertyGuidedGenerator::generateArray(const common::SchemaField& field) {
    size_t size = field.constraints.arraySize.value_or(5);
    common::DataArray arr;
    arr.reserve(size);

    if (!field.children.empty()) {
        for (size_t i = 0; i < size; ++i) {
            arr.push_back(generateField(field.children[0]));
        }
    }
    return common::DataValue(std::move(arr));
}

common::DataValue PropertyGuidedGenerator::generateObject(const common::SchemaField& field) {
    common::DataObject obj;
    for (const auto& child : field.children) {
        obj[child.name] = generateField(child);
    }
    return common::DataValue(std::move(obj));
}

} // namespace generator

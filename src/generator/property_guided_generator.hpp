#pragma once

#include "generator.hpp"
#include <random>
#include <chrono>
#include <functional>
#include <unordered_map>

namespace generator {

struct Property {
    std::string fieldName;
    std::function<common::DataValue()> generator;
};

class PropertyGuidedGenerator : public IGenerator {
public:
    PropertyGuidedGenerator()
        : rng_(std::chrono::steady_clock::now().time_since_epoch().count()) {}

    void loadSchema(const common::Schema& schema) override {
        schema_ = schema;
    }

    // Add custom property generator for a field
    void setProperty(const std::string& fieldName, std::function<common::DataValue()> gen) {
        properties_[fieldName] = gen;
    }

    // Set incrementing counter for a field
    void setCounter(const std::string& fieldName, int64_t start = 0) {
        int64_t counter = start;
        properties_[fieldName] = [counter]() mutable {
            return common::DataValue(counter++);
        };
    }

    // Set timestamp field (current time in ms)
    void setTimestamp(const std::string& fieldName) {
        properties_[fieldName] = []() {
            auto now = std::chrono::system_clock::now();
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count();
            return common::DataValue(static_cast<int64_t>(ms));
        };
    }

    // Set cycling values
    template<typename T>
    void setCycle(const std::string& fieldName, std::vector<T> values) {
        size_t index = 0;
        properties_[fieldName] = [values = std::move(values), index]() mutable {
            auto val = common::DataValue(values[index]);
            index = (index + 1) % values.size();
            return val;
        };
    }

    common::DataValue generate() override {
        common::DataObject obj;
        for (const auto& field : schema_.fields()) {
            obj[field.name] = generateField(field);
        }
        return common::DataValue(std::move(obj));
    }

    Algorithm algorithm() const override { return Algorithm::PropertyGuided; }
    std::string name() const override { return "property_guided"; }

private:
    common::Schema schema_;
    std::mt19937_64 rng_;
    std::unordered_map<std::string, std::function<common::DataValue()>> properties_;

    common::DataValue generateField(const common::SchemaField& field) {
        // Check if there's a custom property for this field
        auto it = properties_.find(field.name);
        if (it != properties_.end()) {
            return it->second();
        }

        // Fall back to schema-based generation
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

    common::DataValue generateInt(const common::FieldConstraints& c) {
        int64_t minVal = static_cast<int64_t>(c.min.value_or(0));
        int64_t maxVal = static_cast<int64_t>(c.max.value_or(1000));
        std::uniform_int_distribution<int64_t> dist(minVal, maxVal);
        return common::DataValue(dist(rng_));
    }

    common::DataValue generateDouble(const common::FieldConstraints& c) {
        double minVal = c.min.value_or(0.0);
        double maxVal = c.max.value_or(1.0);
        std::uniform_real_distribution<double> dist(minVal, maxVal);
        return common::DataValue(dist(rng_));
    }

    common::DataValue generateString(const common::FieldConstraints& c) {
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

    common::DataValue generateBool() {
        std::uniform_int_distribution<int> dist(0, 1);
        return common::DataValue(dist(rng_) == 1);
    }

    common::DataValue generateArray(const common::SchemaField& field) {
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

    common::DataValue generateObject(const common::SchemaField& field) {
        common::DataObject obj;
        for (const auto& child : field.children) {
            obj[child.name] = generateField(child);
        }
        return common::DataValue(std::move(obj));
    }
};

} // namespace generator

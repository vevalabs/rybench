#pragma once

#include "generator.hpp"
#include <random>
#include <functional>
#include <unordered_map>
#include <vector>

namespace generator {

struct Property {
    std::string fieldName;
    std::function<common::DataValue()> generator;
};

class PropertyGuidedGenerator : public IGenerator {
public:
    PropertyGuidedGenerator();

    void loadSchema(const common::Schema& schema) override;
    void setProperty(const std::string& fieldName, std::function<common::DataValue()> gen);
    void setCounter(const std::string& fieldName, int64_t start = 0);
    void setTimestamp(const std::string& fieldName);

    template<typename T>
    void setCycle(const std::string& fieldName, std::vector<T> values) {
        size_t index = 0;
        properties_[fieldName] = [values = std::move(values), index]() mutable {
            auto val = common::DataValue(values[index]);
            index = (index + 1) % values.size();
            return val;
        };
    }

    common::DataValue generate() override;
    Algorithm algorithm() const override;
    std::string name() const override;

private:
    common::Schema schema_;
    std::mt19937_64 rng_;
    std::unordered_map<std::string, std::function<common::DataValue()>> properties_;

    common::DataValue generateField(const common::SchemaField& field);
    common::DataValue generateInt(const common::FieldConstraints& c);
    common::DataValue generateDouble(const common::FieldConstraints& c);
    common::DataValue generateString(const common::FieldConstraints& c);
    common::DataValue generateBool();
    common::DataValue generateArray(const common::SchemaField& field);
    common::DataValue generateObject(const common::SchemaField& field);
};

} // namespace generator

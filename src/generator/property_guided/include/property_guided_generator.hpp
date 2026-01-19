#pragma once

#include "generator.hpp"
#include "reflection_builder.hpp"
#include <random>
#include <functional>
#include <unordered_map>
#include <vector>
#include <memory>

namespace rybench {
namespace generator {

class PropertyGuidedGenerator : public IGenerator {
public:
    PropertyGuidedGenerator();

    void loadSchema(const common::FlatBuffersSchema& schema) override;

    // Set custom generators for specific field paths
    void setIntProperty(const std::string& fieldPath, std::function<int64_t(std::mt19937_64&)> gen);
    void setDoubleProperty(const std::string& fieldPath, std::function<double(std::mt19937_64&)> gen);
    void setStringProperty(const std::string& fieldPath, std::function<std::string(std::mt19937_64&)> gen);

    // Convenience methods
    void setCounter(const std::string& fieldPath, int64_t start = 0);
    void setTimestamp(const std::string& fieldPath);
    void setFixedValue(const std::string& fieldPath, int64_t value);
    void setFixedValue(const std::string& fieldPath, double value);
    void setFixedValue(const std::string& fieldPath, const std::string& value);

    template<typename T>
    void setCycle(const std::string& fieldPath, std::vector<T> values) {
        if constexpr (std::is_integral_v<T>) {
            size_t index = 0;
            setIntProperty(fieldPath, [values = std::move(values), index](std::mt19937_64&) mutable {
                auto val = static_cast<int64_t>(values[index]);
                index = (index + 1) % values.size();
                return val;
            });
        } else if constexpr (std::is_floating_point_v<T>) {
            size_t index = 0;
            setDoubleProperty(fieldPath, [values = std::move(values), index](std::mt19937_64&) mutable {
                auto val = static_cast<double>(values[index]);
                index = (index + 1) % values.size();
                return val;
            });
        }
    }

    void setCycle(const std::string& fieldPath, std::vector<std::string> values) {
        size_t index = 0;
        setStringProperty(fieldPath, [values = std::move(values), index](std::mt19937_64&) mutable {
            auto val = values[index];
            index = (index + 1) % values.size();
            return val;
        });
    }

    common::Message generate() override;
    Algorithm algorithm() const override;
    std::string name() const override;

private:
    const common::FlatBuffersSchema* schema_ = nullptr;
    std::mt19937_64 rng_;
    std::unique_ptr<ReflectionBuilder> builder_;
};

} // namespace generator
} // namespace rybench

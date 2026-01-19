#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <variant>
#include <memory>

namespace rybench {
namespace common {

class DataValue;

using DataObject = std::unordered_map<std::string, DataValue>;
using DataArray = std::vector<DataValue>;

class DataValue {
public:
    using ValueType = std::variant<
        std::nullptr_t,
        bool,
        int64_t,
        double,
        std::string,
        DataArray,
        DataObject
    >;

    DataValue() : value_(nullptr) {}
    DataValue(std::nullptr_t) : value_(nullptr) {}
    DataValue(bool v) : value_(v) {}
    DataValue(int v) : value_(static_cast<int64_t>(v)) {}
    DataValue(int64_t v) : value_(v) {}
    DataValue(double v) : value_(v) {}
    DataValue(const char* v) : value_(std::string(v)) {}
    DataValue(const std::string& v) : value_(v) {}
    DataValue(std::string&& v) : value_(std::move(v)) {}
    DataValue(const DataArray& v) : value_(v) {}
    DataValue(DataArray&& v) : value_(std::move(v)) {}
    DataValue(const DataObject& v) : value_(v) {}
    DataValue(DataObject&& v) : value_(std::move(v)) {}

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(value_); }
    bool isBool() const { return std::holds_alternative<bool>(value_); }
    bool isInt() const { return std::holds_alternative<int64_t>(value_); }
    bool isDouble() const { return std::holds_alternative<double>(value_); }
    bool isString() const { return std::holds_alternative<std::string>(value_); }
    bool isArray() const { return std::holds_alternative<DataArray>(value_); }
    bool isObject() const { return std::holds_alternative<DataObject>(value_); }

    bool asBool() const { return std::get<bool>(value_); }
    int64_t asInt() const { return std::get<int64_t>(value_); }
    double asDouble() const { return std::get<double>(value_); }
    const std::string& asString() const { return std::get<std::string>(value_); }
    const DataArray& asArray() const { return std::get<DataArray>(value_); }
    const DataObject& asObject() const { return std::get<DataObject>(value_); }

    DataArray& asArray() { return std::get<DataArray>(value_); }
    DataObject& asObject() { return std::get<DataObject>(value_); }

    const ValueType& value() const { return value_; }

private:
    ValueType value_;
};

} // namespace common
} // namespace rybench

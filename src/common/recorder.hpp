#pragma once

#include "data_value.hpp"
#include <fstream>
#include <nlohmann/json.hpp>

namespace common {

// Streaming recorder - writes line by line (JSONL format)
class Recorder {
public:
    Recorder() = default;
    ~Recorder() {
        close();
    }

    void open(const std::string& filepath) {
        file_.open(filepath, std::ios::out);
        if (!file_.is_open()) {
            throw std::runtime_error("Failed to create dump file: " + filepath);
        }
        count_ = 0;
    }

    void record(const DataValue& value) {
        if (!file_.is_open()) return;
        file_ << toJson(value).dump() << "\n";
        count_++;
    }

    void close() {
        if (file_.is_open()) {
            file_.close();
        }
    }

    size_t count() const {
        return count_;
    }

    bool isOpen() const {
        return file_.is_open();
    }

private:
    std::ofstream file_;
    size_t count_ = 0;

    static nlohmann::json toJson(const DataValue& v) {
        if (v.isNull()) return nullptr;
        if (v.isBool()) return v.asBool();
        if (v.isInt()) return v.asInt();
        if (v.isDouble()) return v.asDouble();
        if (v.isString()) return v.asString();
        if (v.isArray()) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& item : v.asArray()) {
                arr.push_back(toJson(item));
            }
            return arr;
        }
        if (v.isObject()) {
            nlohmann::json obj = nlohmann::json::object();
            for (const auto& [key, val] : v.asObject()) {
                obj[key] = toJson(val);
            }
            return obj;
        }
        return nullptr;
    }
};

} // namespace common

#include "recorder.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace rybench {
namespace common {

namespace {

nlohmann::json toJson(const DataValue& v) {
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

} // anonymous namespace

Recorder::~Recorder() {
    close();
}

void Recorder::open(const std::string& filepath) {
    file_.open(filepath, std::ios::out);
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to create dump file: " + filepath);
    }
    count_ = 0;
}

void Recorder::record(const DataValue& value) {
    if (!file_.is_open()) return;
    file_ << toJson(value).dump() << "\n";
    count_++;
}

void Recorder::close() {
    if (file_.is_open()) {
        file_.close();
    }
}

} // namespace common
} // namespace rybench

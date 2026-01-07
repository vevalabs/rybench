#include "fixed_generator.hpp"
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace generator {

FixedGenerator::~FixedGenerator() {
    if (file_.is_open()) {
        file_.close();
    }
}

void FixedGenerator::loadSchema(const common::Schema& schema) {
    schema_ = schema;
    if (!file_.is_open() && !useSchemaData_) {
        useSchemaData_ = true;
        schemaData_ = buildFixedFromSchema();
    }
}

void FixedGenerator::loadDump(const std::string& filepath) {
    file_.open(filepath, std::ios::in);
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to open dump file: " + filepath);
    }
    useSchemaData_ = false;
    finished_ = false;
}

common::DataValue FixedGenerator::generate() {
    if (finished_) {
        return common::DataValue(nullptr);
    }

    if (useSchemaData_) {
        return schemaData_;
    }

    std::string line;
    if (std::getline(file_, line)) {
        if (line.empty()) {
            return generate();
        }
        try {
            nlohmann::json j = nlohmann::json::parse(line);
            return jsonToDataValue(j);
        } catch (const std::exception&) {
            return generate();
        }
    }

    finished_ = true;
    return common::DataValue(nullptr);
}

void FixedGenerator::reset() {
    if (file_.is_open()) {
        file_.clear();
        file_.seekg(0, std::ios::beg);
    }
    finished_ = false;
}

bool FixedGenerator::finished() const {
    return finished_;
}

Algorithm FixedGenerator::algorithm() const {
    return Algorithm::Fixed;
}

std::string FixedGenerator::name() const {
    return "fixed";
}

common::DataValue FixedGenerator::buildFixedFromSchema() {
    common::DataObject obj;
    for (const auto& field : schema_.fields()) {
        obj[field.name] = buildFixedField(field);
    }
    return common::DataValue(std::move(obj));
}

common::DataValue FixedGenerator::buildFixedField(const common::SchemaField& field) {
    switch (field.type) {
        case common::FieldType::Int:
            return common::DataValue(static_cast<int64_t>(field.constraints.min.value_or(0)));
        case common::FieldType::Double:
            return common::DataValue(field.constraints.min.value_or(0.0));
        case common::FieldType::String:
            return common::DataValue(std::string(field.constraints.minLength.value_or(5), 'x'));
        case common::FieldType::Bool:
            return common::DataValue(false);
        case common::FieldType::Array: {
            common::DataArray arr;
            size_t size = field.constraints.arraySize.value_or(1);
            if (!field.children.empty()) {
                for (size_t i = 0; i < size; ++i) {
                    arr.push_back(buildFixedField(field.children[0]));
                }
            }
            return common::DataValue(std::move(arr));
        }
        case common::FieldType::Object: {
            common::DataObject obj;
            for (const auto& child : field.children) {
                obj[child.name] = buildFixedField(child);
            }
            return common::DataValue(std::move(obj));
        }
    }
    return common::DataValue(nullptr);
}

common::DataValue FixedGenerator::jsonToDataValue(const nlohmann::json& j) {
    if (j.is_null()) return common::DataValue(nullptr);
    if (j.is_boolean()) return common::DataValue(j.get<bool>());
    if (j.is_number_integer()) return common::DataValue(j.get<int64_t>());
    if (j.is_number_float()) return common::DataValue(j.get<double>());
    if (j.is_string()) return common::DataValue(j.get<std::string>());
    if (j.is_array()) {
        common::DataArray arr;
        for (const auto& item : j) {
            arr.push_back(jsonToDataValue(item));
        }
        return common::DataValue(std::move(arr));
    }
    if (j.is_object()) {
        common::DataObject obj;
        for (auto it = j.begin(); it != j.end(); ++it) {
            obj[it.key()] = jsonToDataValue(it.value());
        }
        return common::DataValue(std::move(obj));
    }
    return common::DataValue(nullptr);
}

nlohmann::json FixedGenerator::dataValueToJson(const common::DataValue& v) {
    if (v.isNull()) return nullptr;
    if (v.isBool()) return v.asBool();
    if (v.isInt()) return v.asInt();
    if (v.isDouble()) return v.asDouble();
    if (v.isString()) return v.asString();
    if (v.isArray()) {
        nlohmann::json arr = nlohmann::json::array();
        for (const auto& item : v.asArray()) {
            arr.push_back(dataValueToJson(item));
        }
        return arr;
    }
    if (v.isObject()) {
        nlohmann::json obj = nlohmann::json::object();
        for (const auto& [key, val] : v.asObject()) {
            obj[key] = dataValueToJson(val);
        }
        return obj;
    }
    return nullptr;
}

} // namespace generator

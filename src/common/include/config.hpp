#pragma once

#include "flatbuffers_schema.hpp"
#include <string>
#include <unordered_map>

namespace common {

class Config {
public:
    using Section = std::unordered_map<std::string, std::string>;
    using Sections = std::unordered_map<std::string, Section>;

    Config() = default;

    void load(const std::string& filepath);

    std::string get(const std::string& section, const std::string& key,
                    const std::string& defaultValue = "") const;
    int getInt(const std::string& section, const std::string& key, int defaultValue = 0) const;
    double getDouble(const std::string& section, const std::string& key, double defaultValue = 0.0) const;
    bool getBool(const std::string& section, const std::string& key, bool defaultValue = false) const;

    bool hasSection(const std::string& section) const;
    bool hasKey(const std::string& section, const std::string& key) const;

    const Sections& sections() const { return sections_; }
    const FlatBuffersSchema& schema() const { return schema_; }

private:
    Sections sections_;
    FlatBuffersSchema schema_;

    static std::string trim(const std::string& str);
};

} // namespace common

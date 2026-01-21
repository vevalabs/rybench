#pragma once

#include "flatbuffers_schema.hpp"
#include <string>
#include <memory>

namespace rybench {
namespace common {

class Config {
public:
    Config();
    ~Config();
    Config(Config&&) noexcept;
    Config& operator=(Config&&) noexcept;

    void load(const std::string& filepath);

    std::string get(const std::string& section, const std::string& key,
                    const std::string& defaultValue = "") const;
    int getInt(const std::string& section, const std::string& key, int defaultValue = 0) const;
    double getDouble(const std::string& section, const std::string& key, double defaultValue = 0.0) const;
    bool getBool(const std::string& section, const std::string& key, bool defaultValue = false) const;

    bool hasSection(const std::string& section) const;
    bool hasKey(const std::string& section, const std::string& key) const;

    const FlatBuffersSchema& schema() const { return schema_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    FlatBuffersSchema schema_;
};

} // namespace common
} // namespace rybench

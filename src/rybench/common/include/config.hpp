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

    // Resolve and load a node config under configRoot using the systemd-style
    // template convention:
    //   nodeName = "foo"        → configRoot/foo/config.toml      (concrete)
    //                             configRoot/foo@/config.toml     (template fallback, %i = "")
    //   nodeName = "foo@bar"    → configRoot/foo@bar/config.toml  (concrete)
    //                             configRoot/foo@/config.toml     (template fallback, %i = "bar")
    // Inside a template, %i expands to the instance name.
    void loadNode(const std::string& configRoot, const std::string& nodeName);

    std::string get(const std::string& section, const std::string& key,
                    const std::string& defaultValue = "") const;
    int getInt(const std::string& section, const std::string& key, int defaultValue = 0) const;
    double getDouble(const std::string& section, const std::string& key, double defaultValue = 0.0) const;
    bool getBool(const std::string& section, const std::string& key, bool defaultValue = false) const;

    bool hasSection(const std::string& section) const;
    bool hasKey(const std::string& section, const std::string& key) const;

    const FlatBuffersSchema& schema() const { return schema_; }
    const std::string& instanceName() const { return instanceName_; }

private:
    void load(const std::string& filepath);
    void validate();
    std::string expandSpecifiers(const std::string& value) const;

    struct Impl;
    std::unique_ptr<Impl> impl_;
    FlatBuffersSchema schema_;
    std::string instanceName_;
};

} // namespace common
} // namespace rybench

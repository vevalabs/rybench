#include "config.hpp"
#include <toml++/toml.hpp>
#include <stdexcept>
#include <iostream>

namespace rybench {
namespace common {

struct Config::Impl {
    toml::table table;
};

Config::Config() : impl_(std::make_unique<Impl>()) {}
Config::~Config() = default;
Config::Config(Config&&) noexcept = default;
Config& Config::operator=(Config&&) noexcept = default;

void Config::load(const std::string& filepath) {
    try {
        impl_->table = toml::parse_file(filepath);
    } catch (const toml::parse_error& err) {
        throw std::runtime_error("Failed to parse config file: " + std::string(err.what()));
    }

    // Validate required sections (transport-specific sections are optional)
    const char* requiredSections[] = {"runtime", "generator", "schema", "serializer"};
    for (const auto& section : requiredSections) {
        if (!impl_->table.contains(section)) {
            throw std::runtime_error(std::string("Config file should have ") + section + " section");
        }
    }

    // Load FlatBuffers schema from .bfbs file
    std::string bfbsPath = get("schema", "bfbs", "config/schemas/benchmark.bfbs");
    schema_.loadFromBfbs(bfbsPath);

    std::cout << "Schema: " << schema_.rootTableName() << std::endl;
}

std::string Config::get(const std::string& section, const std::string& key,
                        const std::string& defaultValue) const {
    if (auto sec = impl_->table[section].as_table()) {
        if (auto val = (*sec)[key].value<std::string>()) {
            return *val;
        }
    }
    return defaultValue;
}

int Config::getInt(const std::string& section, const std::string& key, int defaultValue) const {
    if (auto sec = impl_->table[section].as_table()) {
        if (auto val = (*sec)[key].value<int64_t>()) {
            return static_cast<int>(*val);
        }
    }
    return defaultValue;
}

double Config::getDouble(const std::string& section, const std::string& key, double defaultValue) const {
    if (auto sec = impl_->table[section].as_table()) {
        if (auto val = (*sec)[key].value<double>()) {
            return *val;
        }
    }
    return defaultValue;
}

bool Config::getBool(const std::string& section, const std::string& key, bool defaultValue) const {
    if (auto sec = impl_->table[section].as_table()) {
        if (auto val = (*sec)[key].value<bool>()) {
            return *val;
        }
    }
    return defaultValue;
}

bool Config::hasSection(const std::string& section) const {
    return impl_->table.contains(section);
}

bool Config::hasKey(const std::string& section, const std::string& key) const {
    if (auto sec = impl_->table[section].as_table()) {
        return sec->contains(key);
    }
    return false;
}

} // namespace common
} // namespace rybench

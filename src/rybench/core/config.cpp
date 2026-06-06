#include <rybench/core/config.hpp>
#include <rybench/core/node_config_path.hpp>
#include <toml++/toml.hpp>
#include <stdexcept>
#include <iostream>
#include <filesystem>

namespace rybench::core {

struct Config::Impl {
    toml::table table;
};

Config::Config() : impl_(std::make_unique<Impl>()) {}
Config::~Config() = default;
Config::Config(Config&&) noexcept = default;
Config& Config::operator=(Config&&) noexcept = default;

static toml::table parseFile(const std::string& filepath) {
    try {
        return toml::parse_file(filepath);
    } catch (const toml::parse_error& err) {
        throw std::runtime_error("Failed to parse config file: " + std::string(err.what()));
    }
}

void Config::load(const std::string& filepath) {
    impl_->table = parseFile(filepath);
    validate();
}

void Config::validate() {
    // Validate required sections (transport-specific sections are optional)
    const char* requiredSections[] = {"runtime", "generator", "schema", "serializer"};
    for (const auto& section : requiredSections) {
        if (!impl_->table.contains(section)) {
            throw std::runtime_error(std::string("Config file should have ") + section + " section");
        }
    }

    schemaPath_ = get("schema", "bfbs", "schemas/benchmark.bfbs");
    std::cout << "Schema: " << schemaPath_ << std::endl;
}

void Config::loadNode(const std::string& configRoot, const std::string& nodeName) {
    namespace fs = std::filesystem;

    std::string resolvedPath = resolveNodeConfigPath(configRoot, nodeName, instanceName_);

    // If the node name has the form "transport:role@instance", look for a base config at
    // configRoot/role@/config.toml and merge it under the transport-specific overlay.
    // This lets transport templates contain only their [transport] section while the
    // shared [runtime]/[schema]/[generator]/[serializer] sections live in the role base.
    auto colonPos = nodeName.find(':');
    if (colonPos != std::string::npos) {
        std::string roleAndInstance = nodeName.substr(colonPos + 1); // "publisher@pub1"
        auto atPos = roleAndInstance.find('@');
        std::string role = (atPos != std::string::npos)
            ? roleAndInstance.substr(0, atPos) : roleAndInstance;

        fs::path basePath = fs::path(configRoot) / (role + "@") / "config.toml";
        if (fs::exists(basePath)) {
            std::cout << "Config (base):    " << basePath.string() << "\n";
            std::cout << "Config (overlay): " << resolvedPath;
            if (!instanceName_.empty()) std::cout << " (instance: " << instanceName_ << ")";
            std::cout << std::endl;

            // Load base, then merge overlay sections on top (section-level replace).
            impl_->table = parseFile(basePath.string());
            auto overlay = parseFile(resolvedPath);
            for (auto&& [key, value] : overlay)
                impl_->table.insert_or_assign(key, value);

            validate();
            return;
        }
    }

    std::cout << "Config: " << resolvedPath;
    if (!instanceName_.empty()) std::cout << " (instance: " << instanceName_ << ")";
    std::cout << std::endl;

    load(resolvedPath);
}

std::string Config::expandSpecifiers(const std::string& value) const {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 1 < value.size()) {
            char spec = value[i + 1];
            if (spec == 'i') {
                result += instanceName_;
                ++i;
            } else if (spec == '%') {
                result += '%';
                ++i;
            } else {
                result += value[i];
            }
        } else {
            result += value[i];
        }
    }
    return result;
}

std::string Config::get(const std::string& section, const std::string& key,
                        const std::string& defaultValue) const {
    if (auto sec = impl_->table[section].as_table()) {
        if (auto val = (*sec)[key].value<std::string>()) {
            return expandSpecifiers(*val);
        }
    }
    return expandSpecifiers(defaultValue);
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

} // namespace rybench::core

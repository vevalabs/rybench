#include "config.hpp"
#include <fstream>
#include <stdexcept>

namespace common {

void Config::load(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + filepath);
    }

    std::string line;
    std::string currentSection;

    while (std::getline(file, line)) {
        line = trim(line);

        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        if (line[0] == '[' && line.back() == ']') {
            currentSection = line.substr(1, line.size() - 2);
            sections_[currentSection] = Section{};
        } else {
            auto delimPos = line.find('=');
            if (delimPos != std::string::npos) {
                std::string key = trim(line.substr(0, delimPos));
                std::string value = trim(line.substr(delimPos + 1));
                sections_[currentSection][key] = value;
            }
        }
    }

    if (sections_.find("runtime") == sections_.end()) {
        throw std::runtime_error("Config file should have runtime section");
    }
    if (sections_.find("publisher") == sections_.end()) {
        throw std::runtime_error("Config file should have publisher section");
    }
    if (sections_.find("generator") == sections_.end()) {
        throw std::runtime_error("Config file should have generator section");
    }
    if (sections_.find("serializer") == sections_.end()) {
        throw std::runtime_error("Config file should have serializer section");
    }
    if (sections_.find("dump") == sections_.end()) {
        throw std::runtime_error("Config file should have dump section");
    }
}

std::string Config::get(const std::string& section, const std::string& key,
                        const std::string& defaultValue) const {
    auto secIt = sections_.find(section);
    if (secIt == sections_.end()) {
        return defaultValue;
    }
    auto keyIt = secIt->second.find(key);
    if (keyIt == secIt->second.end()) {
        return defaultValue;
    }
    return keyIt->second;
}

int Config::getInt(const std::string& section, const std::string& key, int defaultValue) const {
    std::string value = get(section, key);
    if (value.empty()) {
        return defaultValue;
    }
    return std::stoi(value);
}

double Config::getDouble(const std::string& section, const std::string& key, double defaultValue) const {
    std::string value = get(section, key);
    if (value.empty()) {
        return defaultValue;
    }
    return std::stod(value);
}

bool Config::getBool(const std::string& section, const std::string& key, bool defaultValue) const {
    std::string value = get(section, key);
    if (value.empty()) {
        return defaultValue;
    }
    return value == "true" || value == "1" || value == "yes";
}

bool Config::hasSection(const std::string& section) const {
    return sections_.find(section) != sections_.end();
}

bool Config::hasKey(const std::string& section, const std::string& key) const {
    auto secIt = sections_.find(section);
    if (secIt == sections_.end()) {
        return false;
    }
    return secIt->second.find(key) != secIt->second.end();
}

std::string Config::trim(const std::string& str) {
    const char* whitespace = " \t\r\n";
    size_t start = str.find_first_not_of(whitespace);
    if (start == std::string::npos) {
        return "";
    }
    size_t end = str.find_last_not_of(whitespace);
    return str.substr(start, end - start + 1);
}

} // namespace common

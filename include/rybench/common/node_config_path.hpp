#pragma once

#include <filesystem>
#include <stdexcept>
#include <string>

namespace rybench::common {

// Resolve the config file path for a node, following the systemd template convention:
//
//   nodeName = "foo"        → configRoot/foo/config.toml          (concrete)
//                             configRoot/foo@/config.toml         (template fallback, %i = "")
//   nodeName = "foo@bar"    → configRoot/foo@bar/config.toml      (concrete, checked first)
//                             configRoot/foo@/config.toml         (template fallback, %i = "bar")
//
// Returns the resolved path. Throws std::runtime_error if no config is found.
// instanceName is set to the part after '@' in the node name, or cleared if absent.
inline std::string resolveNodeConfigPath(const std::string& configRoot,
                                         const std::string& nodeName,
                                         std::string& instanceName) {
    namespace fs = std::filesystem;

    auto atPos = nodeName.find('@');
    std::string baseName = (atPos != std::string::npos) ? nodeName.substr(0, atPos) : nodeName;
    instanceName = (atPos != std::string::npos) ? nodeName.substr(atPos + 1) : "";

    fs::path concrete = fs::path(configRoot) / nodeName / "config.toml";
    fs::path tmpl     = fs::path(configRoot) / (baseName + "@") / "config.toml";

    if (fs::exists(concrete)) return concrete.string();
    if (fs::exists(tmpl))     return tmpl.string();

    throw std::runtime_error(
        "No config found for node '" + nodeName + "': tried " +
        concrete.string() + " and " + tmpl.string());
}

} // namespace rybench::common

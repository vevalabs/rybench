#include "launcher.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cerr << "Usage: rybench-launch <node-name>...\n"
                  << "  $RYBENCH_CONFIG must point to the config root directory.\n"
                  << "  Example: rybench-launch publisher@pub1 publisher@pub2 subscriber@sub1\n";
        return 1;
    }

    const char* configRootEnv = std::getenv("RYBENCH_CONFIG");
    if (!configRootEnv) {
        std::cerr << "$RYBENCH_CONFIG is not set\n";
        return 1;
    }

    char selfPath[4096] = {};
    std::string binDir;
    if (::readlink("/proc/self/exe", selfPath, sizeof(selfPath) - 1) > 0)
        binDir = std::filesystem::path(selfPath).parent_path().string();

    std::vector<std::string> nodeNames(argv + 1, argv + argc);
    return rybench::launch::launch(configRootEnv, binDir, nodeNames);
}

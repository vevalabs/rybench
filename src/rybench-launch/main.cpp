#include "node_config_path.hpp"
#include <toml++/toml.hpp>

#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

static std::vector<pid_t> g_children;

static void forwardSignal(int sig) {
    for (pid_t pid : g_children) ::kill(pid, sig);
}

// Infer transport from whichever section is present in the config.
static std::string readTransport(const std::string& configPath) {
    auto tbl = toml::parse_file(configPath);
    if (tbl.contains("iceoryx2"))  return "iceoryx2";
    if (tbl.contains("zenoh"))     return "zenoh";
    if (tbl.contains("filewriter")) return "filewriter";
    throw std::runtime_error(
        "Cannot infer transport from '" + configPath +
        "': no [iceoryx2], [zenoh], or [filewriter] section found");
}

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
    std::string configRoot = configRootEnv;

    // Determine directory of this executable for sibling binary resolution.
    std::string binDir;
    char selfPath[4096] = {};
    if (::readlink("/proc/self/exe", selfPath, sizeof(selfPath) - 1) > 0)
        binDir = fs::path(selfPath).parent_path().string();

    // Resolve transport for each node name before forking.
    struct NodeSpec { std::string name; std::string binary; };
    std::vector<NodeSpec> nodes;
    for (int i = 1; i < argc; ++i) {
        std::string nodeName = argv[i];
        std::string instanceName;
        std::string configPath = rybench::common::resolveNodeConfigPath(configRoot, nodeName, instanceName);
        std::string transport  = readTransport(configPath);
        std::string binaryName = "rybench-" + transport;
        fs::path candidate = fs::path(binDir) / binaryName;
        nodes.push_back({nodeName, fs::exists(candidate) ? candidate.string() : binaryName});
    }

    std::cout << "Launching " << nodes.size() << " node(s):" << std::endl;
    for (const auto& n : nodes)
        std::cout << "  " << n.name << "  [" << n.binary << "]\n";

    struct sigaction sa{};
    sa.sa_handler = forwardSignal;
    ::sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    ::sigaction(SIGINT,  &sa, nullptr);
    ::sigaction(SIGTERM, &sa, nullptr);

    for (const auto& node : nodes) {
        pid_t pid = ::fork();
        if (pid < 0) {
            std::cerr << "fork() failed for " << node.name << std::endl;
            forwardSignal(SIGTERM);
            break;
        }
        if (pid == 0) {
            const char* args[] = {node.binary.c_str(), node.name.c_str(), nullptr};
            ::execvp(node.binary.c_str(), const_cast<char* const*>(args));
            std::cerr << "execvp failed for " << node.binary << ": " << strerror(errno) << "\n";
            _exit(127);
        }
        g_children.push_back(pid);
    }

    int exitCode = 0;
    for (pid_t pid : g_children) {
        int status = 0;
        ::waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0) exitCode = WEXITSTATUS(status);
        else if (WIFSIGNALED(status)) exitCode = 1;
    }
    return exitCode;
}

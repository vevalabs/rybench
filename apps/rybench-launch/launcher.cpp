#include "launcher.hpp"
#include <rybench/common/node_config_path.hpp>
#include <toml++/toml.hpp>

#include <csignal>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace rybench::launch {

static std::vector<pid_t> g_children;

static void forwardSignal(int sig) {
    for (pid_t pid : g_children) ::kill(pid, sig);
}

static std::string readTransport(const std::string& configPath) {
    auto tbl = toml::parse_file(configPath);
    if (tbl.contains("iceoryx2"))   return "iceoryx2";
    if (tbl.contains("zenoh"))      return "zenoh";
    if (tbl.contains("filewriter")) return "filewriter";
    throw std::runtime_error(
        "Cannot infer transport from '" + configPath +
        "': no [iceoryx2], [zenoh], or [filewriter] section found");
}

int launch(const std::string& configRoot,
           const std::string& binDir,
           const std::vector<std::string>& nodeNames) {
    struct NodeSpec { std::string name; std::string binary; };
    std::vector<NodeSpec> nodes;

    for (const auto& nodeName : nodeNames) {
        std::string instanceName;
        std::string configPath = rybench::common::resolveNodeConfigPath(configRoot, nodeName, instanceName);
        std::string transport  = readTransport(configPath);
        std::string binaryName = "rybench-" + transport;
        fs::path candidate = fs::path(binDir) / binaryName;
        nodes.push_back({nodeName, fs::exists(candidate) ? candidate.string() : binaryName});
    }

    std::cout << "Launching " << nodes.size() << " node(s):\n";
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
            std::cerr << "fork() failed for " << node.name << "\n";
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

} // namespace rybench::launch

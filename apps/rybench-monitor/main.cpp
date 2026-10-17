// rybench-monitor — offline: read a size-prefixed dump, decode each message per
// format and feed a Reelay past-time monitor (via MonitorSink). Measures monitoring
// throughput (events/s). The decode + monitor logic is shared with the online
// transport consumers through MonitorSink.
//
//   rybench-monitor <json|flatbuffers|protobuf> <schema.bfbs> <dump> <past-spec>

#include "monitor_sink.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: rybench-monitor <json|flatbuffers|protobuf> "
                     "<schema.bfbs> <dump> <past-spec>\n";
        return 2;
    }
    const std::string fmt = argv[1], bfbs = argv[2], dump = argv[3], spec = argv[4];

    std::ifstream f(dump, std::ios::binary | std::ios::ate);
    if (!f) { std::cerr << "cannot open dump: " << dump << "\n"; return 1; }
    const std::streamsize fsz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(fsz > 0 ? static_cast<size_t>(fsz) : 0);
    if (fsz > 0) f.read(reinterpret_cast<char*>(data.data()), fsz);

    rybench::monitor::MonitorSink sink(fmt, bfbs, spec);

    const auto t0 = std::chrono::steady_clock::now();
    size_t off = 0;
    while (off + sizeof(uint32_t) <= data.size()) {
        uint32_t msz;
        std::memcpy(&msz, data.data() + off, sizeof(msz));
        off += sizeof(msz);
        if (off + msz > data.size()) break;
        sink.feed(data.data() + off, msz);
        off += msz;
    }
    const auto dt = std::chrono::steady_clock::now() - t0;
    const double ms =
        std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count() / 1e6;
    const double eps = ms > 0 ? sink.count() / (ms / 1000.0) : 0;
    std::cout << fmt << "  events=" << sink.count() << "  ms=" << ms
              << "  events/s=" << static_cast<long long>(eps)
              << "  true=" << sink.trueRatio() << "%\n";
    return 0;
}

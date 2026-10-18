// rybench-monitor — offline: read a size-prefixed dump and feed a past-time monitor
// (Reelay in-process, or MonPoly as an external process). Measures monitoring
// throughput (events/s).
//
//   rybench-monitor <json|flatbuffers|protobuf> <schema.bfbs> <dump> <spec> [backend] [monpoly-bin]
//
// backend: reelay (default) decodes each dumped message per <format> (json/
// flatbuffers/protobuf) to p/q/r/s and feeds a reelay past-time spec. monpoly
// (spec = MFOTL formula text over p()/q()/r()/s()) needs no decode: the dump must
// already be MonpolySerializer-encoded bytes ([serializer] type = "monpoly" when it
// was generated), so <format>/<schema.bfbs> are unused and accepted only to keep
// the CLI shape uniform. monpoly-bin: monpoly executable, default "monpoly".

#include "monitor_sink.hpp"
#include "monpoly_sink.hpp"

#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "usage: rybench-monitor <json|flatbuffers|protobuf> "
                     "<schema.bfbs> <dump> <spec> [reelay|monpoly] [monpoly-bin]\n";
        return 2;
    }
    const std::string fmt = argv[1], bfbs = argv[2], dump = argv[3], spec = argv[4];
    const std::string backend = argc > 5 ? argv[5] : "reelay";
    const std::string monpolyBin = argc > 6 ? argv[6] : "monpoly";

    std::ifstream f(dump, std::ios::binary | std::ios::ate);
    if (!f) { std::cerr << "cannot open dump: " << dump << "\n"; return 1; }
    const std::streamsize fsz = f.tellg();
    f.seekg(0);
    std::vector<uint8_t> data(fsz > 0 ? static_cast<size_t>(fsz) : 0);
    if (fsz > 0) f.read(reinterpret_cast<char*>(data.data()), fsz);

    std::unique_ptr<rybench::monitor::IMonitorSink> sink;
    if (backend == "monpoly") {
        sink = std::make_unique<rybench::monitor::MonpolySink>(spec, monpolyBin);
    } else if (backend == "reelay") {
        sink = std::make_unique<rybench::monitor::MonitorSink>(fmt, bfbs, spec);
    } else {
        std::cerr << "unknown backend: " << backend << " (expected reelay|monpoly)\n";
        return 2;
    }

    const auto t0 = std::chrono::steady_clock::now();
    size_t off = 0;
    while (off + sizeof(uint32_t) <= data.size()) {
        uint32_t msz;
        std::memcpy(&msz, data.data() + off, sizeof(msz));
        off += sizeof(msz);
        if (off + msz > data.size()) break;
        sink->feed(data.data() + off, msz);
        off += msz;
    }
    const auto dt = std::chrono::steady_clock::now() - t0;
    const double ms =
        std::chrono::duration_cast<std::chrono::nanoseconds>(dt).count() / 1e6;
    const double eps = ms > 0 ? sink->count() / (ms / 1000.0) : 0;
    std::cout << fmt << "  events=" << sink->count() << "  ms=" << ms
              << "  events/s=" << static_cast<long long>(eps)
              << "  true=" << sink->trueRatio() << "%\n";
    return 0;
}

#pragma once
// MonpolySink -- feed a MonPoly (external OCaml process, MFOTL) monitor from
// already-MonPoly-encoded message bytes, as an IMonitorSink alternative to the
// in-process Reelay MonitorSink. MonPoly has no C/C++ API: it is a standalone binary
// that reads a textual event log from stdin and prints one verdict line per
// timepoint to stdout (flushed immediately in -verbose mode). We spawn it once and
// keep the pipes open for the whole run -- stdin/stdout/stderr only; -sig and
// -formula are also handed to it over pipes, via /dev/fd/<N> paths, never a real
// file on disk.
//
// Wire format: the publisher's serializer is MonpolySerializer (see
// src/rybench/serializer/monpoly/), which encodes each message directly as MonPoly
// facts ("p()\n", one line per true boolean field, nothing for false ones). So the
// bytes handed to feed() here are already exactly what MonPoly's log format wants
// for one timepoint's facts -- no decode step, just framing + the pipe protocol.
//
// Log protocol (MonPoly's own grammar, src/log_parser.mly): a timepoint's fact block
// is only known to be complete once the FOLLOWING "@<ts>" token arrives (or EOF for
// the very last block). So advancing to event N's facts also opens event (N+1)'s
// header -- that header is what lets MonPoly close and evaluate event N, whose
// verdict we then read back. The very first header (@0) is opened in the
// constructor; the very last timepoint's verdict is flushed by closing stdin in the
// destructor.

#include "atom_decoder.hpp"   // LatStats, computeStats, IMonitorSink

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <unistd.h>
#include <sys/wait.h>

namespace rybench::monitor {

class MonpolySink : public IMonitorSink {
public:
    // formula: MFOTL text over p()/q()/r()/s(). binPath: monpoly executable --
    // PATH-searched if it has no '/'.
    explicit MonpolySink(const std::string& formula, const std::string& binPath = "monpoly") {
        int sigFd = pipeString("p()\nq()\nr()\ns()\n");
        int formulaFd = pipeString(formula + "\n");
        spawn(binPath, sigFd, formulaFd);
        close(sigFd);
        close(formulaFd);

        // Confirm the child is actually alive (wrong binPath / bad formula both show
        // up as an immediate EOF on our read end otherwise, which would silently look
        // like "monitor always false").
        char banner[512];
        if (!fgets(banner, sizeof(banner), in_))
            throw std::runtime_error("MonpolySink: '" + binPath + "' exited immediately "
                                      "(binary not found, or -sig/-formula rejected)");
        writeLine("@0");
        fflush(out_);
    }

    ~MonpolySink() override {
        if (out_) { fclose(out_); out_ = nullptr; }   // EOF -> flushes the last timepoint
        if (in_) {
            char buf[512];
            while (fgets(buf, sizeof(buf), in_)) {}   // drain it so it's not lost
            fclose(in_);
            in_ = nullptr;
        }
        if (pid_ > 0) { int st; waitpid(pid_, &st, 0); }
    }

    // payload is already MonpolySerializer-encoded facts for this timepoint (zero or
    // more complete "<name>()\n" lines; empty if no atom holds).
    void feed(const uint8_t* payload, uint32_t size) override {
        advance(payload, size);
        lastVerdict_ = readVerdict();
        if (lastVerdict_) ++trueCount_;
        ++count_;
    }

    uint64_t count() const override { return count_; }
    double trueRatio() const override { return count_ ? 100.0 * trueCount_ / count_ : 0.0; }

    // Receive->verdict: write-facts + pipe round-trip to MonPoly and back.
    void feedTimed(const uint8_t* payload, uint32_t size) override {
        auto t0 = std::chrono::steady_clock::now();
        feed(payload, size);
        auto t1 = std::chrono::steady_clock::now();
        proc_.push_back(static_cast<uint32_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()));
    }

    // Stage breakdown for one event, given the publisher's send ns. There is no
    // separate decode step (the bytes are already MonPoly facts), so "parse" is ~0
    // and "monitor" is the whole write+pipe round-trip.
    void feedBreakdown(const uint8_t* payload, uint32_t size, uint64_t sendNs) override {
        auto t0 = std::chrono::steady_clock::now();
        uint64_t inNs = static_cast<uint64_t>(t0.time_since_epoch().count());
        auto t1 = t0;                                          // no decode stage
        advance(payload, size);
        lastVerdict_ = readVerdict();                          // pipe round-trip
        if (lastVerdict_) ++trueCount_;
        auto t2 = std::chrono::steady_clock::now();
        uint64_t verdNs = static_cast<uint64_t>(t2.time_since_epoch().count());
        auto span = [](auto a, auto b) {
            return static_cast<uint32_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
        };
        if (inNs   >= sendNs && inNs   - sendNs < 0xffffffffULL) transp_.push_back(static_cast<uint32_t>(inNs   - sendNs));
        parse_.push_back(span(t0, t1));
        mon_.push_back(span(t1, t2));
        if (verdNs >= sendNs && verdNs - sendNs < 0xffffffffULL) lat_.push_back(static_cast<uint32_t>(verdNs - sendNs));
        ++count_;
    }

    LatStats latencyStats() override { return computeStats(lat_); }
    LatStats procStats()    override { return computeStats(proc_); }
    LatStats transpStats()  override { return computeStats(transp_); }
    LatStats parseStats()   override { return computeStats(parse_); }
    LatStats monStats()     override { return computeStats(mon_); }

private:
    // Writes `content` into a fresh pipe and returns its READ end (the write end is
    // closed here, after writing -- content is tiny so it fits the pipe buffer
    // without blocking). The caller passes that fd to the child as "/dev/fd/<fd>",
    // which the OS resolves through /proc/self/fd -- no file ever touches a
    // directory on disk.
    static int pipeString(const std::string& content) {
        int fds[2];
        if (pipe(fds) != 0) throw std::runtime_error("MonpolySink: pipe() failed");
        size_t off = 0;
        while (off < content.size()) {
            ssize_t w = write(fds[1], content.data() + off, content.size() - off);
            if (w <= 0) { close(fds[0]); close(fds[1]); throw std::runtime_error("MonpolySink: write() failed"); }
            off += static_cast<size_t>(w);
        }
        close(fds[1]);
        return fds[0];
    }

    void spawn(const std::string& bin, int sigFd, int formulaFd) {
        int inPipe[2], outPipe[2];   // inPipe: our write end -> child's stdin
        if (pipe(inPipe) != 0 || pipe(outPipe) != 0)
            throw std::runtime_error("MonpolySink: pipe() failed");
        const std::string sigPath = "/dev/fd/" + std::to_string(sigFd);
        const std::string formulaPath = "/dev/fd/" + std::to_string(formulaFd);
        pid_ = fork();
        if (pid_ < 0) throw std::runtime_error("MonpolySink: fork() failed");
        if (pid_ == 0) {
            dup2(inPipe[0], STDIN_FILENO);
            dup2(outPipe[1], STDOUT_FILENO);
            dup2(outPipe[1], STDERR_FILENO);   // merge stderr so parse errors surface
            close(inPipe[0]); close(inPipe[1]);
            close(outPipe[0]); close(outPipe[1]);
            // sigFd/formulaFd are inherited (no O_CLOEXEC set on pipe fds by default)
            // and referenced by number via /dev/fd, so nothing else to do with them.
            execlp(bin.c_str(), bin.c_str(),
                   "-sig", sigPath.c_str(), "-formula", formulaPath.c_str(),
                   "-verbose", static_cast<char*>(nullptr));
            _exit(127);   // exec failed
        }
        close(inPipe[0]);
        close(outPipe[1]);
        out_ = fdopen(inPipe[1], "w");
        in_  = fdopen(outPipe[0], "r");
        if (!out_ || !in_) throw std::runtime_error("MonpolySink: fdopen failed");
    }

    void writeLine(const std::string& s) {
        fputs(s.c_str(), out_);
        fputc('\n', out_);
    }

    // Write the event's facts (current, already-open timepoint) then open the NEXT
    // timepoint's header -- the only thing that tells MonPoly the current block is
    // complete and triggers its verdict.
    void advance(const uint8_t* payload, uint32_t size) {
        if (size) fwrite(payload, 1, size, out_);
        writeLine("@" + std::to_string(count_ + 1));
        fflush(out_);
    }

    // Scan lines until one of MonPoly's "... (time point N): true|false" verdict
    // lines; skips the one-time startup banner and the per-tp "At time point N:"
    // header line transparently (neither contains "time point ... ):").
    bool readVerdict() {
        char buf[512];
        while (fgets(buf, sizeof(buf), in_)) {
            std::string_view line(buf);
            auto tp = line.find("time point");
            if (tp == std::string_view::npos) continue;
            auto colon = line.find("):", tp);
            if (colon == std::string_view::npos) continue;
            auto rest = line.substr(colon + 2);
            if (rest.find("true") != std::string_view::npos) return true;
            if (rest.find("false") != std::string_view::npos) return false;
        }
        return lastVerdict_;   // pipe closed unexpectedly -- hold last known value
    }

    int pid_ = -1;
    FILE* out_ = nullptr;   // child's stdin
    FILE* in_  = nullptr;   // child's stdout (+ stderr, merged)

    uint64_t count_ = 0;
    uint64_t trueCount_ = 0;
    bool lastVerdict_ = false;
    std::vector<uint32_t> lat_, proc_, transp_, parse_, mon_;
};

}  // namespace rybench::monitor

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
// header -- that header is what lets MonPoly close and evaluate event N. The very
// first header (@0) is opened in the constructor.
//
// Write/read are on SEPARATE threads, not lockstep. A PAST-only formula resolves
// event N's verdict as soon as event (N+1)'s header arrives (delay 1); a
// FUTURE-bounded one (e.g. EVENTUALLY[0,10]) only resolves event N once events up to
// N+10 have been written -- writing N and then blocking to read N's verdict before
// writing N+1 would deadlock (N's verdict can't exist until N+1..N+10 are written,
// which can't happen until this call returns). So feed() only writes; a background
// reader thread drains verdicts as they become available and matches each one back
// (FIFO -- MonPoly emits them in timepoint order) to the written-at timestamp (and
// optional publisher send-ns) recorded for that event. finish() closes stdin (EOF
// flushes the last pending verdicts) and joins the reader so count()/trueRatio()/
// *Stats() see everything once it returns.

#include "atom_decoder.hpp"   // LatStats, computeStats, IMonitorSink

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
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
        reader_ = std::thread(&MonpolySink::readerLoop, this);
    }

    ~MonpolySink() override {
        finish();
        if (pid_ > 0) { int st; waitpid(pid_, &st, 0); pid_ = -1; }
    }

    void feed(const uint8_t* payload, uint32_t size) override {
        advance(payload, size);
        pushPending(0, 0, false);
    }

    uint64_t count() const override { return count_; }
    double trueRatio() const override { return count_ ? 100.0 * trueCount_ / count_ : 0.0; }

    void feedTimed(const uint8_t* payload, uint32_t size) override {
        const uint64_t t0 = nowNs();
        advance(payload, size);
        pushPending(t0, 0, false);
    }

    void feedBreakdown(const uint8_t* payload, uint32_t size, uint64_t sendNs) override {
        const uint64_t t0 = nowNs();
        advance(payload, size);
        pushPending(t0, sendNs, true);
    }

    // Closes stdin (EOF lets MonPoly resolve and flush every still-pending
    // timepoint) and joins the reader thread. Idempotent.
    void finish() override {
        if (out_) { fclose(out_); out_ = nullptr; }
        if (reader_.joinable()) reader_.join();
    }

    LatStats latencyStats() override { return computeStats(lat_); }
    LatStats procStats()    override { return computeStats(proc_); }
    LatStats transpStats()  override { return computeStats(transp_); }
    LatStats parseStats()   override { return computeStats(parse_); }
    LatStats monStats()     override { return computeStats(mon_); }

private:
    static uint64_t nowNs() {
        return static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }

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
    // complete. Write-only: does NOT wait for a verdict (see class comment).
    void advance(const uint8_t* payload, uint32_t size) {
        if (size) fwrite(payload, 1, size, out_);
        writeLine("@" + std::to_string(writeIdx_ + 1));
        fflush(out_);
        ++writeIdx_;
    }

    struct Pending { uint64_t writeNs; uint64_t sendNs; bool hasSend; };

    void pushPending(uint64_t writeNs, uint64_t sendNs, bool hasSend) {
        std::lock_guard<std::mutex> lk(pendingMu_);
        pending_.push_back({writeNs, sendNs, hasSend});
    }

    // Runs for the whole life of the sink: scans every line MonPoly prints, and for
    // each "... (time point N): true|false" verdict (skipping the one-time startup
    // banner and the per-tp "At time point N:" header line, neither of which
    // contains "time point ... ):"), pops the oldest pending write and records it.
    // MonPoly emits verdicts strictly in timepoint order, matching the order feed()
    // pushed them, so FIFO popping is always the correct match.
    void readerLoop() {
        char buf[512];
        while (fgets(buf, sizeof(buf), in_)) {
            std::string_view line(buf);
            auto tp = line.find("time point");
            if (tp == std::string_view::npos) continue;
            auto colon = line.find("):", tp);
            if (colon == std::string_view::npos) continue;
            auto rest = line.substr(colon + 2);
            bool verdict;
            if (rest.find("true") != std::string_view::npos) verdict = true;
            else if (rest.find("false") != std::string_view::npos) verdict = false;
            else continue;

            const uint64_t verdNs = nowNs();
            Pending p{0, 0, false};
            {
                std::lock_guard<std::mutex> lk(pendingMu_);
                if (pending_.empty()) continue;   // shouldn't happen; be defensive
                p = pending_.front();
                pending_.pop_front();
            }
            if (verdict) ++trueCount_;
            ++count_;
            if (p.writeNs) {
                proc_.push_back(nsSpan(p.writeNs, verdNs));
                if (p.hasSend) {
                    if (p.writeNs >= p.sendNs) transp_.push_back(nsSpan(p.sendNs, p.writeNs));
                    mon_.push_back(nsSpan(p.writeNs, verdNs));
                    if (verdNs >= p.sendNs) lat_.push_back(nsSpan(p.sendNs, verdNs));
                }
            }
        }
        fclose(in_);
        in_ = nullptr;
    }

    static uint32_t nsSpan(uint64_t a, uint64_t b) {
        return static_cast<uint32_t>(std::min<uint64_t>(b - a, 0xffffffffULL));
    }

    int pid_ = -1;
    FILE* out_ = nullptr;   // child's stdin (main thread only)
    FILE* in_  = nullptr;   // child's stdout+stderr (reader thread only, after construction)
    uint64_t writeIdx_ = 0; // next timepoint header to open (main thread only)

    std::thread reader_;
    std::mutex pendingMu_;
    std::deque<Pending> pending_;              // written, awaiting a verdict (FIFO)

    std::atomic<uint64_t> count_{0};
    std::atomic<uint64_t> trueCount_{0};
    std::vector<uint32_t> lat_, proc_, transp_, parse_, mon_;  // reader thread only
};

}  // namespace rybench::monitor

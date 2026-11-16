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
// Write/read are on SEPARATE threads, not lockstep. A FUTURE-bounded formula (e.g.
// EVENTUALLY[0,10]) only resolves timepoint N once blocks up to ~N+10 are closed, so
// writing N and blocking for N's verdict would deadlock. feed() only writes; a
// background reader thread drains MonPoly's output. finish() closes stdin (EOF
// flushes the last pending verdicts) and joins the reader.
//
// Latency is measured against the TRIGGERING event, not the event the verdict is
// about. In -verbose mode MonPoly prints "At time point K:" when block K closes,
// then every verdict that closing resolved ("@ts. (time point N): true|false", one
// per timepoint for a closed formula -- algorithm.ml show_results). Block K is
// closed by feed(K) (it writes K's facts plus header K+1), so each verdict is
// stamped against feed(K)'s write time / send-ns -- the monitor's reaction time to
// the arrival that produced the verdict, excluding the lookahead wait. Verdicts
// emitted after EOF have no triggering feed and are counted but not timed.

#include "atom_decoder.hpp"   // LatStats, computeStats, IMonitorSink

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include <cerrno>

#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

namespace rybench::monitor {

class MonpolySink : public IMonitorSink {
public:
    // formula: MFOTL text over p()/q()/r()/s(). binPath: monpoly executable --
    // PATH-searched if it has no '/'. busyPoll: the reader thread spins on a
    // non-blocking read instead of sleeping in the kernel between verdicts -- costs
    // a full core, but each verdict no longer pays the wake-up of a halted CPU
    // (measured ~10us of p90 at 100kHz, present even with only C1 allowed).
    explicit MonpolySink(const std::string& formula, const std::string& binPath = "monpoly",
                         bool busyPoll = false)
        : busyPoll_(busyPoll) {
        int sigFd = pipeString("p()\nq()\nr()\ns()\n");
        int formulaFd = pipeString(formula + "\n");
        spawn(binPath, sigFd, formulaFd);
        close(sigFd);
        close(formulaFd);

        // Confirm the child is actually alive (wrong binPath / bad formula both show
        // up as an immediate EOF on our read end otherwise, which would silently look
        // like "monitor always false").
        std::string_view banner;
        if (!readLine(banner))
            throw std::runtime_error("MonpolySink: '" + binPath + "' exited immediately "
                                      "(binary not found, or -sig/-formula rejected)");
        if (busyPoll_) fcntl(inFd_, F_SETFL, fcntl(inFd_, F_GETFL) | O_NONBLOCK);
        writeLine("@0");
        fflush(out_);
        reader_ = std::thread(&MonpolySink::readerLoop, this);
    }

    ~MonpolySink() override {
        finish();
        if (pid_ > 0) { int st; waitpid(pid_, &st, 0); pid_ = -1; }
    }

    // The record is pushed BEFORE writing, so it exists by the time the reader can
    // see the "At time point K" line this write produces.
    void feed(const uint8_t* payload, uint32_t size) override {
        record(0, 0, false);
        advance(payload, size);
    }

    uint64_t count() const override { return count_; }
    double trueRatio() const override { return count_ ? 100.0 * trueCount_ / count_ : 0.0; }

    void feedTimed(const uint8_t* payload, uint32_t size) override {
        record(nowNs(), 0, false);
        advance(payload, size);
    }

    void feedBreakdown(const uint8_t* payload, uint32_t size, uint64_t sendNs) override {
        record(nowNs(), sendNs, true);
        advance(payload, size);
    }

    // Closes stdin (EOF lets MonPoly resolve and flush every still-pending
    // timepoint) and joins the reader thread. Idempotent.
    void finish() override {
        if (out_) { fclose(out_); out_ = nullptr; }
        if (reader_.joinable()) reader_.join();
        if (!warned_ && (gaps_ || count_ != writeIdx_)) {
            warned_ = true;
            fprintf(stderr, "[MonpolySink] WARNING: %llu events fed, %llu verdicts, "
                            "%llu timepoint gaps\n",
                    static_cast<unsigned long long>(writeIdx_),
                    static_cast<unsigned long long>(count_.load()),
                    static_cast<unsigned long long>(gaps_));
        }
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
        inFd_ = outPipe[0];
        if (!out_) throw std::runtime_error("MonpolySink: fdopen failed");
    }

    // Next '\n'-terminated line from MonPoly's output, as a view into rbuf_ that is
    // valid until the next call; false at EOF. In busy-poll mode the fd is
    // non-blocking and an empty pipe spins here instead of sleeping in read().
    bool readLine(std::string_view& out) {
        for (;;) {
            const size_t nl = rbuf_.find('\n', rpos_);
            if (nl != std::string::npos) {
                out = std::string_view(rbuf_).substr(rpos_, nl + 1 - rpos_);
                rpos_ = nl + 1;
                return true;
            }
            if (rpos_) { rbuf_.erase(0, rpos_); rpos_ = 0; }
            char tmp[4096];
            const ssize_t n = read(inFd_, tmp, sizeof(tmp));
            if (n > 0) { rbuf_.append(tmp, static_cast<size_t>(n)); continue; }
            if (n == 0) return false;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
#if defined(__x86_64__) || defined(__i386__)
                __builtin_ia32_pause();
#endif
                continue;
            }
            if (errno == EINTR) continue;
            return false;
        }
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

    struct Rec { uint64_t writeNs; uint64_t sendNs; bool hasSend; };

    void record(uint64_t writeNs, uint64_t sendNs, bool hasSend) {
        std::lock_guard<std::mutex> lk(recMu_);
        recs_.push_back({writeNs, sendNs, hasSend});   // index == writeIdx_ (timepoint)
    }

    static bool parseIndex(std::string_view s, size_t from, uint64_t& out) {
        if (from >= s.size() || s[from] < '0' || s[from] > '9') return false;
        out = 0;
        while (from < s.size() && s[from] >= '0' && s[from] <= '9')
            out = out * 10 + static_cast<uint64_t>(s[from++] - '0');
        return true;
    }

    // "At time point K:" -> trigger = K. "@ts. (time point N): true|false" -> one
    // verdict for timepoint N, timed against trigger K's record.
    void readerLoop() {
        static constexpr std::string_view kAt = "At time point ";
        static constexpr std::string_view kTp = "(time point ";
        std::string_view line;
        int64_t trigger = -1;
        while (readLine(line)) {
            uint64_t k;
            if (line.substr(0, kAt.size()) == kAt) {
                if (parseIndex(line, kAt.size(), k)) trigger = static_cast<int64_t>(k);
                continue;
            }
            if (line.empty() || line[0] != '@') continue;
            auto tp = line.find(kTp);
            uint64_t n;
            if (tp == std::string_view::npos || !parseIndex(line, tp + kTp.size(), n)) continue;
            auto colon = line.find("):", tp);
            if (colon == std::string_view::npos) continue;
            auto rest = line.substr(colon + 2);
            bool verdict;
            if (rest.find("true") != std::string_view::npos) verdict = true;
            else if (rest.find("false") != std::string_view::npos) verdict = false;
            else continue;

            const uint64_t verdNs = nowNs();
            Rec r{0, 0, false};
            bool timed = false;
            {
                std::lock_guard<std::mutex> lk(recMu_);
                // The header feed() always pre-opens leaves one empty trailing block
                // that EOF closes; it is no real event, so its verdict is dropped.
                if (n >= recs_.size()) continue;
                if (trigger >= 0 && static_cast<size_t>(trigger) < recs_.size()) {
                    r = recs_[static_cast<size_t>(trigger)];
                    timed = true;
                }
            }
            if (n != nextTp_) ++gaps_;
            nextTp_ = n + 1;
            if (verdict) ++trueCount_;
            ++count_;
            if (!timed) continue;
            if (!r.writeNs) continue;
            proc_.push_back(nsSpan(r.writeNs, verdNs));          // trigger write -> verdict
            if (r.hasSend) {
                if (r.writeNs >= r.sendNs) transp_.push_back(nsSpan(r.sendNs, r.writeNs));
                mon_.push_back(nsSpan(r.writeNs, verdNs));
                if (verdNs >= r.sendNs) lat_.push_back(nsSpan(r.sendNs, verdNs));
            }
        }
        close(inFd_);
        inFd_ = -1;
    }

    static uint32_t nsSpan(uint64_t a, uint64_t b) {
        return static_cast<uint32_t>(std::min<uint64_t>(b - a, 0xffffffffULL));
    }

    int pid_ = -1;
    FILE* out_ = nullptr;   // child's stdin (main thread only)
    int   inFd_ = -1;       // child's stdout+stderr (reader thread only, after construction)
    std::string rbuf_;      // readLine buffer (reader thread only, after construction)
    size_t rpos_ = 0;
    bool busyPoll_ = false;
    uint64_t writeIdx_ = 0; // next timepoint header to open (main thread only)

    std::thread reader_;
    std::mutex recMu_;
    std::vector<Rec> recs_;                    // per fed timepoint (main pushes, reader reads)

    std::atomic<uint64_t> count_{0};
    std::atomic<uint64_t> trueCount_{0};
    uint64_t nextTp_ = 0;                      // reader thread only
    uint64_t gaps_ = 0;                        // verdicts whose timepoint skipped ahead
    bool warned_ = false;
    std::vector<uint32_t> lat_, proc_, transp_, parse_, mon_;  // reader thread only
};

}  // namespace rybench::monitor

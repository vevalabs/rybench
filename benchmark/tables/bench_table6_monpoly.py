#!/usr/bin/env python3
"""Table 6 -- MonPoly ONLINE monitoring latency per transport x property.

Same setup as Table 5, but the monitor backend is MonPoly (external OCaml process,
MFOTL) instead of Reelay (in-process, linked library). A rybench publisher
generates + serializes + publishes a trace over a transport (iceoryx2 / zenoh); a
rybench consumer subscribes and feeds the already-MonPoly-encoded bytes (the wire
serializer is "monpoly", see src/rybench/serializer/monpoly/) to MonpolySink, which
keeps a persistent `monpoly -verbose` subprocess open for the whole run.

Latency is per TRIGGERING event: each verdict is timed from the publish timestamp
(8-byte steady_clock prefix, stamp_time / monitor_latency) of the event whose arrival
closed the block that produced it -- the monitor's reaction time. The inherent wait
for a bounded-future formula's window (~bound * publish interval) is excluded; it
says nothing about MonPoly. The last ~bound verdicts resolve only at EOF and are not
timed.

Each cell is also measured with build/bin/monpoly-echo -- a stand-in that speaks
MonPoly's protocol but answers "true" instantly -- so  delta = MonPoly p50 - echo p50
is MonPoly's own per-event cost, the rest being transport + pipe + process overhead.

Defaults are the paper setup: 200k events at 100kHz, 3 runs per cell (median), all
processes pinned to CPUs 0-7 (the P-cores of the i5-13500H this was measured on --
change --pin for another machine), and on iceoryx2 the MonpolySink reader thread
busy-polls (monitor_busy_poll) instead of sleeping between verdicts. Without it,
waking that thread on a halted core adds a ~10us p90 mode (present even with only
C1 allowed). zenoh is left blocking: its own stack dominates (~15us) and a spinning
reader there competes with zenoh's threads, raising p50.

    python3 benchmark/tables/bench_table6_monpoly.py                    # paper table
    python3 benchmark/tables/bench_table6_monpoly.py --latex t6.tex      # also save LaTeX
    python3 benchmark/tables/bench_table6_monpoly.py --gen random --repeat 1 --count 20000   # quick check

Needs: build/bin/{rybench-iceoryx2,rybench-zenoh,rybench-filewriter,monpoly-echo} and
the monpoly binary (rv-tools/monpoly/_build/default/src/main.exe, or --monitor-bin).
"""
import argparse, os, re, signal, statistics, subprocess, sys, tempfile, time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN  = os.path.join(REPO, "build", "bin")

_MONPOLY_CANDIDATES = [
    os.path.join(REPO, "rv-tools", "monpoly", "_build", "default", "src", "main.exe"),
    "monpoly",
]
MONPOLY_BIN = next((p for p in _MONPOLY_CANDIDATES if os.path.exists(p)), _MONPOLY_CANDIDATES[-1])
ECHO_BIN = os.path.join(BIN, "monpoly-echo")

# Direct MFOTL transcriptions of each schema's own FUTURE-time formula (the exact
# "mtl:" attribute the property_guided generator is built to satisfy -- see
# benchmark/mtl/<dir>/schema.fbs), with the outer unbounded "always[0,inf]" dropped:
# MonPoly rejects an unbounded future operator outright ("not monitorable"), and we
# don't need it anyway -- checking the bounded inner implication AT EVERY timepoint
# *is* the "always", without conjoining verdicts across time. This also avoids the
# past-time reformulation (historically/once/since, a mechanical translation of
# Reelay's spec): that one is artifact-prone (co-designed by timescales against a
# specific trace template, not a faithful equivalent of the future formula for an
# arbitrary trace) and conjoins every verdict (PAST_ALWAYS), so a single violation
# zeroes true% for the rest of an arbitrarily long trace. Each verdict here is
# independent per timepoint. MonPoly resolves timepoint t once ~t+bound is closed:
# a fixed lag of 11 blocks (21 for response_between's nested UNTILs).
_PROPDEFS = [
    ("response_globally",  "bounded_response_globally",
     "p() IMPLIES (EVENTUALLY[0,10] s())"),
    ("absence_after_q",     "bounded_absence_after_q",
     "q() IMPLIES (ALWAYS[0,10] (NOT p()))"),
    ("response_between",    "bounded_response_between_q_and_r",
     "(q() AND ((NOT r()) AND (EVENTUALLY[0,10] r()))) IMPLIES "
     "(p() IMPLIES ((((NOT r()) UNTIL[0,10] s()) UNTIL[0,10] r())))"),
]
PROPS = [(label, f"benchmark/mtl/{d}/schema.bfbs", formula) for label, d, formula in _PROPDEFS]

TRANSPORTS = [
    ("iceoryx2", "rybench-iceoryx2", "iceoryx2",
     'service = "Bench"\ninstance = "Default"\nevent = "Data"\n'),
    ("zenoh", "rybench-zenoh", "zenoh",
     'key_expr = "Bench/Default/Data"\n'),
]

GEN_LABEL = {"mtl": "Property-guided (MTL)", "random": "Schema-guided (random)",
             "fixed": "Fixed replay"}

def make_dump(bfbs, count, path):
    cfg = (f'[runtime]\napp_name = "n"\n[schema]\nbfbs = "{bfbs}"\n'
           f'[generator]\nalgorithm = "schema_guided"\nbackend = "flatbuffers"\nseed = 1\ncount = {count}\n'
           f'[serializer]\ntype = "monpoly"\n[filewriter]\noutput = "{path}"\nrate_hz = 0\n')
    with tempfile.TemporaryDirectory() as tmp:
        d = os.path.join(tmp, "n"); os.makedirs(d)
        open(os.path.join(d, "config.toml"), "w").write(cfg)
        subprocess.run([os.path.join(BIN, "rybench-filewriter"), "n"], cwd=REPO,
                       env=dict(os.environ, RYBENCH_CONFIG=tmp),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)

def gen_ser(gen, count, dump=None):
    if gen == "fixed":
        # dump was created with [serializer] type = "monpoly" (make_dump), so the
        # replayed bytes are already-encoded facts. [generator] backend tags the
        # FixedGenerator's replayed Message with a format (default "flatbuffers" --
        # engine.cpp always creates this serializer, regardless of algorithm); it
        # must be "monpoly" too or the [serializer] below misreads the format tag
        # and tries to parse the fact text as a FlatBuffer.
        return (f'[generator]\nalgorithm = "fixed"\nbackend = "monpoly"\nseed = 1\ncount = {count}\n'
                f'replay = "{dump}"\n[serializer]\ntype = "monpoly"\n')
    algo = "schema_guided" if gen == "random" else "property_guided"
    return (f'[generator]\nalgorithm = "{algo}"\nbackend = "flatbuffers"\n'
            f'seed = 1\ncount = {count}\n[serializer]\ntype = "monpoly"\n')

def common(bfbs, count, gen="mtl", dump=None):
    return f'[runtime]\napp_name = "n"\n[schema]\nbfbs = "{bfbs}"\n' + gen_ser(gen, count, dump)

def pub_cfg(section, base, bfbs, count, rate, gen, dump=None):
    tport = (f'[{section}]\nmode = "publish"\nmax_payload_size = 4096\nrate_hz = {rate}\n'
             f'{base}stamp_time = "true"\n')
    return common(bfbs, count, gen, dump) + tport

def sub_cfg(section, base, bfbs, formula, mon_bin, busy_poll):
    # No monitor_format/bfbs needed: wire bytes are already MonpolySerializer-encoded
    # facts, so MonpolySink feeds them straight through -- no decode on this side.
    return (common(bfbs, 0) +
            f'[{section}]\nmode = "consume"\nmax_payload_size = 4096\n'
            f'monitor = "true"\nmonitor_backend = "monpoly"\n'
            f'monitor_monpoly_bin = "{mon_bin}"\n'
            f'monitor_busy_poll = "{"true" if busy_poll else "false"}"\n'
            f'monitor_spec = "{formula}"\nmonitor_latency = "true"\n{base}')

def run_cell(binary, section, base, bfbs, formula, count, rate, gen, dump, mon_bin, pin,
             busy_poll):
    # pin: CPU list for taskset (e.g. "0-7" = P-cores only); the monitor process
    # spawned by the subscriber inherits the subscriber's affinity.
    prefix = ["taskset", "-c", pin] if pin else []
    with tempfile.TemporaryDirectory() as tmp:
        for name, text in (("pub", pub_cfg(section, base, bfbs, count, rate, gen, dump)),
                           ("sub", sub_cfg(section, base, bfbs, formula, mon_bin, busy_poll))):
            d = os.path.join(tmp, name); os.makedirs(d)
            open(os.path.join(d, "config.toml"), "w").write(text)
        env = dict(os.environ, RYBENCH_CONFIG=tmp)
        exe = os.path.join(BIN, binary)
        sub = subprocess.Popen(prefix + [exe, "sub"], cwd=REPO, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)                                   # let the subscriber + monitor process start
        # Publish can be slower than rate (property_guided retries) or faster than
        # MonPoly can drain -- scale the budget to count, with a generous floor.
        pub_timeout = max(300, count / max(rate, 1) * 20 + 60)
        subprocess.run(prefix + [exe, "pub"], cwd=REPO, env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=pub_timeout)
        drain = max(2.0, count / 100_000)                  # MonPoly's own ceiling is ~250-450k/s
        time.sleep(drain)
        sub.send_signal(signal.SIGINT)
        try:
            out, _ = sub.communicate(timeout=max(30, drain * 2))
        except subprocess.TimeoutExpired:
            sub.kill(); out, _ = sub.communicate()
    return out

def parse_run(out):
    # recv = messages the consumer received (count - recv = publish-side loss,
    # DiscardSample). n = latency samples (verdicts some arriving event triggered).
    rm = re.search(r"Received (\d+) messages", out)
    lm = re.search(r"latency \(ns\): n=(\d+) p50=(\d+) p90=(\d+) p99=(\d+)", out)
    tm = re.search(r"true=([\d.]+)%", out)
    if not (rm and lm and tm):
        return None
    return dict(recv=int(rm.group(1)), timed=int(lm.group(1)), p50=int(lm.group(2)),
                p90=int(lm.group(3)), p99=int(lm.group(4)), tru=float(tm.group(1)))

def measure(args, gen, bfbs, formula, binary, section, base, dump, mon_bin, busy):
    runs = []
    for _ in range(args.repeat):
        r = parse_run(run_cell(binary, section, base, bfbs, formula, args.count, args.rate,
                               gen, dump, mon_bin, args.pin, busy))
        if r:
            runs.append(r)
    if not runs:
        return None
    med = lambda k: statistics.median(x[k] for x in runs)
    return dict(recv=int(med("recv")), timed=int(med("timed")), tru=med("tru"),
                p50=med("p50"), p90=med("p90"), p99=med("p99"),
                p50s=[x["p50"] for x in runs], runs=len(runs))

us = lambda ns: ns / 1000.0

def print_paper_tables(rows, args, with_echo):
    cols = ["generator", "property", "transport", "p50", "p90", "p99"]
    if with_echo:
        cols += ["echo p50", "Δ p50"]
    print("\n=== Paper table: MonPoly online monitoring latency (µs, median of "
          f"{args.repeat} runs) ===\n")
    print("| " + " | ".join(cols) + " |")
    print("|" + "|".join("---" if i < 3 else "---:" for i in range(len(cols))) + "|")
    prev_gen = prev_prop = None
    for r in rows:
        m, e = r["mon"], r["echo"]
        g = GEN_LABEL[r["gen"]] if r["gen"] != prev_gen else ""
        p = r["prop"] if (r["gen"], r["prop"]) != prev_prop else ""
        prev_gen, prev_prop = r["gen"], (r["gen"], r["prop"])
        cells = [g, p, r["transport"]]
        cells += [f"{us(m[k]):.1f}" for k in ("p50", "p90", "p99")] if m else ["-"] * 3
        if with_echo:
            cells += ([f"{us(e['p50']):.1f}", f"{us(m['p50'] - e['p50']):.1f}"]
                      if (m and e) else ["-", "-"])
        print("| " + " | ".join(cells) + " |")

    tex = lambda s: s.replace("_", r"\_")
    gens = [g for g in dict.fromkeys(r["gen"] for r in rows)]
    spec = "lllrrr" + ("rr" if with_echo else "")
    head = ["generator", "property", "transport", "p50", "p90", "p99"]
    if with_echo:
        head += ["echo p50", r"$\Delta$"]
    busy = ", ".join(sorted(args.busy_set)) or "none"
    L = [r"% needs \usepackage{booktabs,multirow}",
         r"\begin{table}[t]", r"\centering",
         r"\caption{MonPoly online monitoring latency ($\mu$s, median of "
         f"{args.repeat} runs; {args.count:,} events at {args.rate // 1000}\\,kHz). "
         r"Latency is from the publish of the event that triggers a verdict to the "
         r"verdict being read by the consumer."
         + (r" Echo: a monitor speaking MonPoly's protocol that answers \texttt{true} "
            r"instantly; $\Delta$ = MonPoly p50 $-$ echo p50, MonPoly's own cost."
            if with_echo else "")
         + f" Monitor reader busy-polls on: {tex(busy)}." + "}",
         rf"\begin{{tabular}}{{{spec}}}", r"\toprule", " & ".join(head) + r" \\", r"\midrule"]
    for gi, gen in enumerate(gens):
        grp = [r for r in rows if r["gen"] == gen]
        for i, r in enumerate(grp):
            m, e = r["mon"], r["echo"]
            c1 = rf"\multirow{{{len(grp)}}}{{*}}{{{GEN_LABEL[gen]}}}" if i == 0 else ""
            first_prop = i == 0 or grp[i - 1]["prop"] != r["prop"]
            nprop = sum(1 for x in grp if x["prop"] == r["prop"])
            c2 = rf"\multirow{{{nprop}}}{{*}}{{{tex(r['prop'])}}}" if first_prop else ""
            cells = [c1, c2, r["transport"]]
            cells += [f"{us(m[k]):.1f}" for k in ("p50", "p90", "p99")] if m else ["--"] * 3
            if with_echo:
                cells += ([f"{us(e['p50']):.1f}", f"{us(m['p50'] - e['p50']):.1f}"]
                          if (m and e) else ["--", "--"])
            L.append(" & ".join(cells) + r" \\")
        if gi < len(gens) - 1:
            L.append(r"\midrule")
    L += [r"\bottomrule", r"\end{tabular}", r"\end{table}"]
    latex = "\n".join(L)
    print("\n=== LaTeX ===\n")
    print(latex)
    if args.latex:
        open(args.latex, "w").write(latex + "\n")
        print(f"\n(LaTeX also written to {args.latex})")

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--count", type=int, default=200_000, help="events per run")
    ap.add_argument("--rate", type=int, default=100_000, help="publish rate (Hz)")
    ap.add_argument("--gen", choices=["mtl", "random", "fixed", "all"], default="all",
                    help="publisher generator: random=schema_guided (random atoms), "
                         "fixed=replay a pre-generated 50k dump, mtl=property_guided, "
                         "all=random+fixed")
    ap.add_argument("--repeat", type=int, default=3,
                    help="runs per cell; the table shows the median")
    ap.add_argument("--pin", default="0-7",
                    help="taskset CPU list for pub/sub/monitor ('' = no pinning). 0-7 = "
                         "P-cores of an i5-13500H -- check /sys/devices/cpu_core/cpus")
    ap.add_argument("--busy-poll", default="iceoryx2",
                    help="comma list of transports whose monitor reader thread busy-polls "
                         "(monitor_busy_poll); '' = none")
    ap.add_argument("--monitor-bin", default=MONPOLY_BIN, help="monpoly executable")
    ap.add_argument("--echo-bin", default=ECHO_BIN,
                    help="baseline monitor answering true instantly (build/bin/monpoly-echo)")
    ap.add_argument("--no-echo", action="store_true", help="skip the echo baseline runs")
    ap.add_argument("--latex", default=None, help="also write the LaTeX table to this file")
    args = ap.parse_args()
    args.busy_set = {t.strip() for t in args.busy_poll.split(",") if t.strip()}
    sys.stdout.reconfigure(line_buffering=True)   # rows appear as cells finish, even when redirected

    with_echo = not args.no_echo
    for path, what in ((args.monitor_bin, "monpoly"), (args.echo_bin if with_echo else None, "echo")):
        if path and "/" in path and not os.path.exists(path):
            sys.exit(f"{what} binary not found: {path}")

    cpu = next((l.split(":", 1)[1].strip() for l in open("/proc/cpuinfo")
                if l.startswith("model name")), "?")
    gens = ["random", "fixed"] if args.gen == "all" else [args.gen]
    ncells = len(gens) * len(PROPS) * len(TRANSPORTS)
    print(f"cpu={cpu}  kernel={os.uname().release}")
    print(f"count={args.count:,}  rate={args.rate:,}Hz  repeat={args.repeat}  "
          f"pin={args.pin or 'none'}  busy_poll={','.join(sorted(args.busy_set)) or 'none'}  "
          f"echo={'yes' if with_echo else 'no'}")
    print(f"{ncells} cells x {args.repeat} runs x {2 if with_echo else 1} monitors\n")

    hdr = (f"  {'property':<19}{'transport':<10}{'recv':>9}{'drop':>6}{'true%':>8}"
           f"{'p50':>8}{'p90':>8}{'p99':>8}   {'p50 per run':<26}"
           + (f"{'echo p50':>9}{'Δ':>7}" if with_echo else "") + "   (µs)")
    rows = []
    for gen in gens:
        dumpdir = tempfile.mkdtemp() if gen == "fixed" else None
        print(GEN_LABEL[gen])
        print(hdr); print("  " + "-" * (len(hdr) - 2))
        for label, bfbs, formula in PROPS:
            dump = None
            if gen == "fixed":
                dump = os.path.join(dumpdir, os.path.basename(os.path.dirname(bfbs)) + ".dump")
                make_dump(bfbs, min(args.count, 50_000), dump)
            for tname, binary, section, base in TRANSPORTS:
                if not os.path.exists(os.path.join(BIN, binary)):
                    print(f"  {label:<19}{tname:<10}(binary missing)"); continue
                busy = tname in args.busy_set
                m = measure(args, gen, bfbs, formula, binary, section, base, dump,
                            args.monitor_bin, busy)
                e = (measure(args, gen, bfbs, formula, binary, section, base, dump,
                             args.echo_bin, busy) if with_echo else None)
                rows.append(dict(gen=gen, prop=label, transport=tname, mon=m, echo=e))
                if not m:
                    print(f"  {label:<19}{tname:<10}(no result)"); continue
                line = (f"  {label:<19}{tname:<10}{m['recv']:>9,}{args.count - m['recv']:>6,}"
                        f"{m['tru']:>7.1f}%{us(m['p50']):>8.1f}{us(m['p90']):>8.1f}"
                        f"{us(m['p99']):>8.1f}   "
                        f"{' / '.join(f'{us(x):.1f}' for x in m['p50s']):<26}")
                if with_echo:
                    line += (f"{us(e['p50']):>9.1f}{us(m['p50'] - e['p50']):>7.1f}" if e
                             else f"{'-':>9}{'-':>7}")
                print(line)
        print()

    print_paper_tables(rows, args, with_echo)

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Table 6 -- MonPoly ONLINE monitoring latency per transport x property.

Same setup as Table 5, but the monitor backend is MonPoly (external OCaml process,
MFOTL) instead of Reelay (in-process, linked library). A rybench publisher
generates + serializes + publishes an MTL trace over a transport (iceoryx2 / zenoh);
a rybench consumer subscribes, decodes each message, and feeds it to MonpolySink,
which keeps a persistent `monpoly -verbose` subprocess open for the whole run,
writing one event per round-trip and reading back its true/false verdict. Latency
(ns) is publish->verdict end-to-end, via the same 8-byte steady_clock send-timestamp
prefix used for the Reelay table (stamp_time / monitor_latency).

MonPoly has no C/C++ API, so there is no Table-4-style offline/in-process variant to
compare against here (see MonpolySink's own docstring for the log protocol and the
mechanical reelay-DSL -> MFOTL translation of each property's past-time spec).

    benchmark/tables/bench_table6_monpoly.py --gen all
"""
import argparse, os, re, signal, subprocess, sys, tempfile, time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN  = os.path.join(REPO, "build", "bin")

_MONPOLY_CANDIDATES = [
    os.path.join(REPO, "rv-tools", "monpoly", "_build", "default", "src", "main.exe"),
    "monpoly",
]
MONPOLY_BIN = next((p for p in _MONPOLY_CANDIDATES if os.path.exists(p)), _MONPOLY_CANDIDATES[-1])

# MFOTL translations of the same past-time specs used for Table 5's Reelay monitor
# (historically/once/since/not/and -> PAST_ALWAYS/ONCE/SINCE/NOT/AND, "->" ->
# IMPLIES, "[a:]" -> "[a,*)"), over the same 0-ary atoms p()/q()/r()/s(). Verified to
# agree with Reelay's true-ratio on both MTL-generated and random traces for all
# three properties before wiring this table (see commit message for the readings).
_PROPDEFS = [
    ("response_globally",  "bounded_response_globally",
     "PAST_ALWAYS ((s() IMPLIES (ONCE[0,10] p())) AND (NOT ((NOT s()) SINCE[10,*) p())))"),
    ("absence_after_q",     "bounded_absence_after_q",
     "PAST_ALWAYS ((ONCE[0,10] q()) IMPLIES ((NOT p()) SINCE q()))"),
    ("response_between",    "bounded_response_between_q_and_r",
     "PAST_ALWAYS ((r() AND (NOT q()) AND (ONCE q())) IMPLIES "
     "( (((s() IMPLIES (ONCE[0,10] p())) AND (NOT ((NOT s()) SINCE[10,*) p()))) SINCE q()) ))"),
]
PROPS = [(label, f"benchmark/mtl/{d}/schema.bfbs", formula) for label, d, formula in _PROPDEFS]

TRANSPORTS = [
    ("iceoryx2", "rybench-iceoryx2", "iceoryx2",
     'service = "Bench"\ninstance = "Default"\nevent = "Data"\n'),
    ("zenoh", "rybench-zenoh", "zenoh",
     'key_expr = "Bench/Default/Data"\n'),
]
FMT = "json"   # MonpolySink decodes json|flatbuffers|protobuf alike; json keeps it simple

def make_dump(bfbs, count, path):
    cfg = (f'[runtime]\napp_name = "n"\n[schema]\nbfbs = "{bfbs}"\n'
           f'[generator]\nalgorithm = "schema_guided"\nbackend = "flatbuffers"\nseed = 1\ncount = {count}\n'
           f'[serializer]\ntype = "{FMT}"\n[filewriter]\noutput = "{path}"\nrate_hz = 0\n')
    with tempfile.TemporaryDirectory() as tmp:
        d = os.path.join(tmp, "n"); os.makedirs(d)
        open(os.path.join(d, "config.toml"), "w").write(cfg)
        subprocess.run([os.path.join(BIN, "rybench-filewriter"), "n"], cwd=REPO,
                       env=dict(os.environ, RYBENCH_CONFIG=tmp),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)

def gen_ser(gen, count, dump=None):
    if gen == "fixed":
        return (f'[generator]\nalgorithm = "fixed"\nseed = 1\ncount = {count}\n'
                f'replay = "{dump}"\n[serializer]\ntype = "flatbuffers"\n')
    algo = "schema_guided" if gen == "random" else "property_guided"
    return (f'[generator]\nalgorithm = "{algo}"\nbackend = "flatbuffers"\n'
            f'seed = 1\ncount = {count}\n[serializer]\ntype = "{FMT}"\n')

def common(bfbs, count, gen="mtl", dump=None):
    return f'[runtime]\napp_name = "n"\n[schema]\nbfbs = "{bfbs}"\n' + gen_ser(gen, count, dump)

def pub_cfg(section, base, bfbs, count, rate, gen, dump=None):
    tport = (f'[{section}]\nmode = "publish"\nmax_payload_size = 4096\nrate_hz = {rate}\n'
             f'{base}stamp_time = "true"\n')
    return common(bfbs, count, gen, dump) + tport

def sub_cfg(section, base, bfbs, formula):
    return (common(bfbs, 0) +
            f'[{section}]\nmode = "consume"\nmax_payload_size = 4096\n'
            f'monitor = "true"\nmonitor_format = "{FMT}"\nmonitor_backend = "monpoly"\n'
            f'monitor_monpoly_bin = "{MONPOLY_BIN}"\n'
            f'monitor_spec = "{formula}"\nmonitor_latency = "true"\n{base}')

def run_cell(binary, section, base, bfbs, formula, count, rate, gen, dump=None):
    with tempfile.TemporaryDirectory() as tmp:
        for name, text in (("pub", pub_cfg(section, base, bfbs, count, rate, gen, dump)),
                           ("sub", sub_cfg(section, base, bfbs, formula))):
            d = os.path.join(tmp, name); os.makedirs(d)
            open(os.path.join(d, "config.toml"), "w").write(text)
        env = dict(os.environ, RYBENCH_CONFIG=tmp)
        exe = os.path.join(BIN, binary)
        sub = subprocess.Popen([exe, "sub"], cwd=REPO, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)                                   # let the subscriber + monpoly process start
        subprocess.run([exe, "pub"], cwd=REPO, env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
        time.sleep(2.0)                                   # let the consumer drain (monpoly is slow)
        sub.send_signal(signal.SIGINT)
        try:
            out, _ = sub.communicate(timeout=30)
        except subprocess.TimeoutExpired:
            sub.kill(); out, _ = sub.communicate()
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=5_000,
                    help="events per run — MonPoly's process round-trip is much slower "
                         "than Reelay's in-process call, so this is far smaller than "
                         "Table 5's default")
    ap.add_argument("--rate", type=int, default=2_000,
                    help="publish rate (Hz), kept below MonPoly's own throughput ceiling")
    ap.add_argument("--gen", choices=["mtl", "random", "fixed", "all"], default="all",
                    help="publisher generator: mtl=property_guided (real MTL traces), "
                         "random=schema_guided (RANDOM atoms, perf-only), "
                         "fixed=replay a pre-generated dump, all=random+fixed back to back "
                         "(matches the two sections of the reference table)")
    args = ap.parse_args()

    if not os.path.exists(MONPOLY_BIN) and "/" in MONPOLY_BIN:
        print(f"warning: monpoly binary not found at {MONPOLY_BIN}", file=sys.stderr)

    gens = ["random", "fixed"] if args.gen == "all" else [args.gen]
    for gen in gens:
        run_table(gen, args)

GEN_LABEL = {"mtl": "property_guided MTL", "random": "Schema-guided (random)",
             "fixed": "Fixed replay"}

def run_table(gen, args):
    dumpdir = tempfile.mkdtemp() if gen == "fixed" else None

    def dump_for(bfbs):
        if gen != "fixed":
            return None
        p = os.path.join(dumpdir, os.path.basename(os.path.dirname(bfbs)) + ".dump")
        if not os.path.exists(p):
            make_dump(bfbs, min(args.count, 50_000), p)
        return p

    print(f"{GEN_LABEL[gen]}")
    hdr = f"  {'property':<22}{'transport':<11}{'recv':>8}{'p50':>10}{'p90':>10}{'p99':>10}  (ns)"
    print(hdr); print("  " + "-" * (len(hdr) - 2))
    for label, bfbs, formula in PROPS:
        for tname, binary, section, base in TRANSPORTS:
            if not os.path.exists(os.path.join(BIN, binary)):
                print(f"  {label:<22}{tname:<11}(binary missing)"); continue
            dump = dump_for(bfbs)
            out = run_cell(binary, section, base, bfbs, formula, args.count, args.rate, gen, dump)
            # monitor_latency=true -> the consumer prints "Monitor latency (ns): n=<N>
            # p50=... p90=... p99=..." on shutdown (N = latency samples received), not
            # an "events=" line (that's only the non-latency summary).
            lm = re.search(r"latency \(ns\): n=(\d+) p50=(\d+) p90=(\d+) p99=(\d+)", out)
            recv = int(lm.group(1)) if lm else 0
            row = f"  {label:<22}{tname:<11}{recv:>8,}"
            if lm:
                row += f"{int(lm.group(2)):>10,}{int(lm.group(3)):>10,}{int(lm.group(4)):>10,}"
            else:
                row += f"{'-':>10}{'-':>10}{'-':>10}"
            print(row)
    print()

if __name__ == "__main__":
    main()

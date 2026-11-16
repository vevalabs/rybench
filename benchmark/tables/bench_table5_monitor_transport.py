#!/usr/bin/env python3
"""Table 5 -- Reelay ONLINE monitoring perf per transport x format.

A rybench publisher generates + serializes + publishes an MTL trace over a transport
(iceoryx2 / zenoh); a rybench consumer subscribes, decodes each message and feeds a
Reelay past-time monitor (rybench's MonitorSink). Reelay is used as-is (library, no
source changes). Two metrics per cell:
  - throughput : events/s the monitor sustains over the transport
  - latency    : publish->monitor end-to-end (ns), p50/p99/max, via an 8-byte
                 steady_clock send-timestamp prefix (stamp_time / monitor_latency)
Compare against Table 4 (offline, no transport) to isolate the transport's cost.
"""
import argparse, os, re, signal, subprocess, sys, tempfile, time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN  = os.path.join(REPO, "build", "bin")

# Past spec is the timescales CO-DESIGNED past formula (same source verify_traces.py
# uses), NOT a hand-rolled mirror. lower/upper bound = 0/10 to match the schema formula.
sys.path.insert(0, os.path.join(REPO, "benchmark", "tools", "timescales", "src"))
import timescales  # noqa: E402

def past_spec(prop):
    past, _future, _rows = timescales.generate(
        property=prop, lower_bound=0, upper_bound=10,
        min_recur=1, max_recur=7, duration=10, limit_stutter=0, failing_end=False)
    return past

# (label, schema dir under benchmark/mtl, timescales property name). Same easy/hard/complex
# split as Table 3; the reelay past spec is generated from the property name above.
_PROPDEFS = [
    ("fast (response_globally)",   "bounded_response_globally",        "response_globally"),
    ("hard (absence_after_q)",     "bounded_absence_after_q",          "absence_after_q"),
    ("complex (response_between)", "bounded_response_between_q_and_r",  "response_between_q_and_r"),
]
PROPS = [(label, f"benchmark/mtl/{d}/schema.bfbs", past_spec(prop))
         for label, d, prop in _PROPDEFS]

TRANSPORTS = [
    ("iceoryx2", "rybench-iceoryx2", "iceoryx2",
     'service = "Bench"\ninstance = "Default"\nevent = "Data"\n'),
    ("zenoh", "rybench-zenoh", "zenoh",
     'key_expr = "Bench/Default/Data"\n'),
]
FORMATS = ["json"]

def backend_for(f):
    return f if f in ("protobuf", "flatbuffers") else "flatbuffers"

def make_dump(bfbs, fmt, count, path):
    """Pre-generate a serialized dump (format fmt) for fixed-replay publishing, so the
    publisher's generation cost is removed and the monitor/transport becomes the ceiling."""
    cfg = (common(bfbs, fmt, count) +
           f'[filewriter]\noutput = "{path}"\nrate_hz = 0\n')
    with tempfile.TemporaryDirectory() as tmp:
        d = os.path.join(tmp, "n"); os.makedirs(d)
        open(os.path.join(d, "config.toml"), "w").write(cfg)
        subprocess.run([os.path.join(BIN, "rybench-filewriter"), "n"], cwd=REPO,
                       env=dict(os.environ, RYBENCH_CONFIG=tmp),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)

def gen_ser(gen, fmt, count, dump=None):
    # gen: mtl=property_guided (real MTL traces), random=schema_guided (RANDOM atom
    # values — perf only, does NOT satisfy the property), fixed=replay a dump.
    if gen == "fixed":
        return (f'[generator]\nalgorithm = "fixed"\nseed = 1\ncount = {count}\n'
                f'replay = "{dump}"\n[serializer]\ntype = "flatbuffers"\n')
    algo = "schema_guided" if gen == "random" else "property_guided"
    return (f'[generator]\nalgorithm = "{algo}"\nbackend = "{backend_for(fmt)}"\n'
            f'seed = 1\ncount = {count}\n[serializer]\ntype = "{fmt}"\n')

def common(bfbs, fmt, count, gen="mtl", dump=None):
    return (f'[runtime]\napp_name = "n"\n[schema]\nbfbs = "{bfbs}"\n'
            + gen_ser(gen, fmt, count, dump))

def pub_cfg(section, base, bfbs, fmt, count, latency, rate, gen, dump=None):
    extra = 'stamp_time = "true"\n' if latency else ''
    tport = (f'[{section}]\nmode = "publish"\nmax_payload_size = 4096\nrate_hz = {rate}\n'
             f'{base}{extra}')
    return common(bfbs, fmt, count, gen, dump) + tport

def sub_cfg(section, base, bfbs, spec, fmt, latency):
    extra = 'monitor_latency = "true"\n' if latency else ''
    return (common(bfbs, fmt, 0) +
            f'[{section}]\nmode = "consume"\nmax_payload_size = 4096\n'
            f'monitor = "true"\nmonitor_format = "{fmt}"\nmonitor_spec = "{spec}"\n'
            f'{base}{extra}')

def run_cell(binary, section, base, bfbs, spec, fmt, count, latency, rate, gen, dump=None):
    with tempfile.TemporaryDirectory() as tmp:
        for name, text in (("pub", pub_cfg(section, base, bfbs, fmt, count, latency, rate, gen, dump)),
                           ("sub", sub_cfg(section, base, bfbs, spec, fmt, latency))):
            d = os.path.join(tmp, name); os.makedirs(d)
            open(os.path.join(d, "config.toml"), "w").write(text)
        env = dict(os.environ, RYBENCH_CONFIG=tmp)
        exe = os.path.join(BIN, binary)
        sub = subprocess.Popen([exe, "sub"], cwd=REPO, env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
        time.sleep(1.0)                                   # let the subscriber attach
        subprocess.run([exe, "pub"], cwd=REPO, env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
        time.sleep(2.0)                                   # let the consumer drain
        sub.send_signal(signal.SIGINT)
        try:
            out, _ = sub.communicate(timeout=15)
        except subprocess.TimeoutExpired:
            sub.kill(); out, _ = sub.communicate()
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=300_000)
    ap.add_argument("--latency", action="store_true", help="also measure publish->monitor latency")
    ap.add_argument("--lat-rate", type=int, default=100_000,
                    help="publish rate (Hz) for latency runs — kept below transport "
                         "capacity so neither transport saturates (fair intrinsic latency)")
    ap.add_argument("--lat-count", type=int, default=30_000,
                    help="events per latency run (throttled, so fewer than throughput)")
    ap.add_argument("--gen", choices=["mtl", "random", "fixed", "all"], default="mtl",
                    help="publisher generator: mtl=property_guided (real MTL traces), "
                         "random=schema_guided (RANDOM atoms, perf-only, does NOT satisfy "
                         "the property), fixed=replay a pre-generated dump (generation free), "
                         "all=run mtl+random+fixed back to back")
    ap.add_argument("--fixed", action="store_true", help="alias for --gen fixed")
    args = ap.parse_args()

    gen_arg = "fixed" if args.fixed else args.gen
    gens = ["mtl", "random", "fixed"] if gen_arg == "all" else [gen_arg]
    for gen in gens:
        run_tables(gen, args)


def run_tables(gen, args):
    dumpdir = tempfile.mkdtemp() if gen == "fixed" else None

    def dump_for(bfbs, fmt):
        if gen != "fixed":
            return None
        p = os.path.join(dumpdir, f"{os.path.basename(os.path.dirname(bfbs))}_{fmt}.dump")
        if not os.path.exists(p):
            make_dump(bfbs, fmt, min(args.count, 50_000), p)
        return p

    genlabel = {"mtl": "property_guided MTL", "random": "schema_guided RANDOM (perf only)",
                "fixed": "FIXED replay"}[gen]
    print("=" * 70)
    print(f"GENERATOR: {genlabel}   (latency open-loop at {args.lat_rate:,} Hz)")
    print("=" * 70)
    for label, bfbs, spec in PROPS:
        print(f"property: {label}   spec: {spec}")
        hdr = (f"  {'transport':<11}{'format':<13}{'recv':>10}{'dropN':>9}"
               f"{'lat%':>7}{'true%':>7}{'p50':>10}{'p90':>10}{'p99':>10}{'max':>11}"
               f"{'tport':>10}{'parse':>8}{'mon':>8}  (ns)")
        print(hdr); print("  " + "-" * (len(hdr) - 2))
        for tname, binary, section, base in TRANSPORTS:
            if not os.path.exists(os.path.join(BIN, binary)):
                print(f"  {tname:<11}(binary missing)"); continue
            for fmt in FORMATS:
                dump = dump_for(bfbs, fmt)
                lo = run_cell(binary, section, base, bfbs, spec, fmt,
                              args.count, latency=True, rate=args.lat_rate, gen=gen, dump=dump)
                m  = re.search(r"events=(\d+) ms=[\d.]+ events/s=(\d+)", lo)
                lm = re.search(r"n=(\d+) p50=(\d+) p90=(\d+) p99=(\d+) max=(\d+)", lo)
                tm = re.search(r"true=([\d.]+)%", lo)
                bm = re.search(r"breakdown p50 \(ns\): transport=(\d+) parse=(\d+) monitor=(\d+)", lo)
                recv = int(m.group(1)) if m else 0
                dropn = args.count - recv
                row = f"  {tname:<11}{fmt:<13}{recv:>10,}{dropn:>9,}"
                if lm:
                    ln = int(lm.group(1))
                    latpct = 100.0 * ln / args.count if args.count else 0.0
                    tru = float(tm.group(1)) if tm else 0.0
                    row += (f"{latpct:>6.1f}%{tru:>6.1f}%{int(lm.group(2)):>10,}{int(lm.group(3)):>10,}"
                            f"{int(lm.group(4)):>10,}{int(lm.group(5)):>11,}")
                    if bm:
                        row += f"{int(bm.group(1)):>10,}{int(bm.group(2)):>8,}{int(bm.group(3)):>8,}"
                    else:
                        row += f"{'-':>10}{'-':>8}{'-':>8}"
                else:
                    row += f"{'-':>7}{'-':>7}{'-':>10}{'-':>10}{'-':>10}{'-':>11}{'-':>10}{'-':>8}{'-':>8}"
                print(row)
            print()

if __name__ == "__main__":
    main()

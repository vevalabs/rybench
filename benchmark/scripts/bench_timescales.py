#!/usr/bin/env python3
"""Measure timescales trace-generation throughput.

For each MTL property and trace duration, time timescales.generate() and report
events/sec (rows per second). Writes a JSON baseline under benchmark/results/ so
results can be compared backward / used for regression checking.

Usage:
    benchmark/scripts/bench_timescales.py
    benchmark/scripts/bench_timescales.py --durations 1000 100000 1000000 --repeats 7
"""
import sys, os, time, json, random, argparse, platform
from datetime import datetime, timezone

HERE  = os.path.dirname(os.path.abspath(__file__))
BENCH = os.path.dirname(HERE)                              # benchmark/
TS_SRC = os.path.join(BENCH, "tools", "timescales", "src") # cloned tool
sys.path.insert(0, TS_SRC)

try:
    import timescales  # type: ignore  # resolved at runtime via sys.path above
except ImportError:
    sys.exit(f"timescales not found at {TS_SRC}\n"
             f"Clone it: git clone --depth 1 "
             f"https://github.com/doganulus/timescales.git "
             f"{os.path.join(BENCH, 'tools', 'timescales')}")

# property name -> bounds it needs (lbound matters only for *_between_* and response)
# Ordered to match RyBench's sorted MTL cases 1:1 (timescales 'always_*' == our
# 'universality_*'), so the two throughput tables line up row for row.
PROPERTIES = [
    "absence_after_q",             # bounded_absence_after_q
    "absence_before_r",            # bounded_absence_before_r
    "absence_between_q_and_r",     # bounded_absence_between_q_and_r
    "recurrence_between_q_and_r",  # bounded_recurrence_between_q_and_r
    "recurrence_globally",         # bounded_recurrence_globally
    "response_between_q_and_r",    # bounded_response_between_q_and_r
    "response_globally",           # bounded_response_globally
    "always_after_q",              # bounded_universality_after_q
    "always_before_r",             # bounded_universality_before_r
    "always_between_q_and_r",      # bounded_universality_between_q_and_r
]


def bench_one(prop, duration, lbound, ubound, repeats, write_fmt=None, write_dir=None):
    """Time generation (always) and, if write_fmt is set, end-to-end
    generation + write-to-disk — the fair comparison against rybench filewriter."""
    gen_times, e2e_times, rows_count = [], [], 0
    for _ in range(repeats):
        random.seed(0)  # deterministic trace each run
        t0 = time.perf_counter()
        _, _, rows = timescales.generate(
            property=prop, lower_bound=lbound, upper_bound=ubound,
            min_recur=1, max_recur=7, duration=duration,
            limit_stutter=0, failing_end=False)
        t1 = time.perf_counter()
        gen_times.append(t1 - t0)
        rows_count = len(rows)
        if write_fmt:
            t2 = time.perf_counter()
            if write_fmt == "csv":
                timescales.write_csv_trace(rows, filename="_e2e_tmp", directory=write_dir)
            else:
                timescales.write_json_trace(rows, filename="_e2e_tmp", directory=write_dir,
                                            persistent=(write_fmt == "json-persistent"))
            t3 = time.perf_counter()
            e2e_times.append((t1 - t0) + (t3 - t2))  # generate + write
    gen_best = min(gen_times)
    res = {
        "rows": rows_count,
        "best_sec": gen_best,
        "events_per_sec": rows_count / gen_best if gen_best > 0 else 0,
    }
    if write_fmt:
        e2e_best = min(e2e_times)
        res["write_format"] = write_fmt
        res["e2e_sec"] = e2e_best
        res["e2e_events_per_sec"] = rows_count / e2e_best if e2e_best > 0 else 0
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--durations", type=int, nargs="+",
                    default=[1_000, 10_000, 100_000, 1_000_000])
    ap.add_argument("--ubound", type=int, default=100)
    ap.add_argument("--lbound", type=int, default=0)
    ap.add_argument("--repeats", type=int, default=5)
    ap.add_argument("--properties", nargs="+", default=PROPERTIES)
    ap.add_argument("--out",
                    default=os.path.join(BENCH, "results", "timescales_baseline.json"))
    # end-to-end mode: include serialization + file write in the timing
    ap.add_argument("--include-write", action="store_true",
                    help="also measure end-to-end (generate + write trace to disk)")
    ap.add_argument("--write-format", choices=["csv", "json", "json-persistent"],
                    default="csv", help="output format used for the --include-write timing")
    # sample-trace output (so you can inspect the generated data)
    ap.add_argument("--no-traces", dest="save_traces", action="store_false",
                    default=True, help="skip writing sample trace files")
    ap.add_argument("--trace-duration", type=int, default=50)
    ap.add_argument("--trace-ubound", type=int, default=5)
    ap.add_argument("--trace-format", choices=["csv", "json"], default="csv")
    ap.add_argument("--trace-dir",
                    default=os.path.join(BENCH, "results", "traces"))
    args = ap.parse_args()

    # throwaway dir for the end-to-end write measurement (cleaned up after)
    write_dir = None
    if args.include_write:
        import tempfile
        write_dir = tempfile.mkdtemp(prefix="ts_e2e_")

    results = []
    mode = f"gen+write ({args.write_format})" if args.include_write else "gen only"
    print(f"timescales throughput  (ubound={args.ubound}, repeats={args.repeats}, "
          f"best-of, {mode})\n")
    hdr = f"{'property':<28}{'duration':>10}{'rows':>12}{'gen ms':>10}{'gen ev/s':>14}"
    if args.include_write:
        hdr += f"{'e2e ms':>10}{'e2e ev/s':>14}"
    print(hdr)
    print("-" * len(hdr))
    for prop in args.properties:
        for d in args.durations:
            r = bench_one(prop, d, args.lbound, args.ubound, args.repeats,
                          write_fmt=args.write_format if args.include_write else None,
                          write_dir=write_dir)
            r.update(property=prop, duration=d)
            results.append(r)
            line = (f"{prop:<28}{d:>10}{r['rows']:>12}"
                    f"{r['best_sec']*1e3:>10.2f}{r['events_per_sec']:>14,.0f}")
            if args.include_write:
                line += f"{r['e2e_sec']*1e3:>10.2f}{r['e2e_events_per_sec']:>14,.0f}"
            print(line)
        print()

    if write_dir:
        import shutil
        shutil.rmtree(write_dir, ignore_errors=True)

    baseline = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "python": platform.python_version(),
        "machine": platform.machine(),
        "params": {"ubound": args.ubound, "lbound": args.lbound,
                   "repeats": args.repeats,
                   "include_write": args.include_write,
                   "write_format": args.write_format if args.include_write else None},
        "results": results,
    }
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(baseline, f, indent=2)
    print(f"baseline written: {os.path.relpath(args.out, BENCH)}")

    # Write a small, inspectable sample trace per property into results/traces/
    if args.save_traces:
        os.makedirs(args.trace_dir, exist_ok=True)
        for prop in args.properties:
            random.seed(0)
            _, _, rows = timescales.generate(
                property=prop, lower_bound=args.lbound,
                upper_bound=args.trace_ubound, min_recur=1, max_recur=7,
                duration=args.trace_duration, limit_stutter=0, failing_end=False)
            if args.trace_format == "csv":
                timescales.write_csv_trace(rows, filename=prop,
                                           directory=args.trace_dir)
            else:
                timescales.write_json_trace(rows, filename=prop,
                                            directory=args.trace_dir,
                                            persistent=False)
        print(f"sample traces written: "
              f"{os.path.relpath(args.trace_dir, BENCH)}/<property>.{args.trace_format} "
              f"(duration={args.trace_duration}, ubound={args.trace_ubound})")


if __name__ == "__main__":
    main()

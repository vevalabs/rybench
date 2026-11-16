#!/usr/bin/env python3
"""
Reference comparison: generate one trace per MTL property with the *timescales*
tool (Ulus 2019) and report statistics — the counterpart to gen_traces.py, which
uses RyBench's own property-guided generator.

    benchmark/mtl/gen_traces_timescales.py <N>

N = approximate trace length (timescales' --duration). Future-fragment formulas are
used (ubound=100, lbound=0). Each trace is written as tuples to
benchmark/mtl/output_traces_timescales/<name>.tuples and a statistics table is printed.
Unlike our generator, timescales always produces a full, satisfying trace.
"""
import os
import sys
import time

MTL_DIR = os.path.dirname(os.path.abspath(__file__))
REPO    = os.path.dirname(os.path.dirname(MTL_DIR))
TS_SRC  = os.path.join(REPO, "benchmark", "tools", "timescales", "src")
OUT_DIR = os.path.join(MTL_DIR, "output_traces_timescales")

sys.path.insert(0, TS_SRC)
try:
    import timescales  # noqa: E402  (resolved via sys.path above)
except ImportError:
    sys.exit(f"timescales not found at {TS_SRC}\n"
             f"Clone it: git clone --depth 1 "
             f"https://github.com/doganulus/timescales.git "
             f"{os.path.dirname(TS_SRC)}")

UBOUND, LBOUND, MIN_RECUR, MAX_RECUR = 100, 0, 1, 7

# timescales property names, in the same order as gen_traces.py's 10 cases
# (our 'universality_*' is timescales' 'always_*').
PROPERTIES = [
    "absence_after_q",
    "absence_before_r",
    "absence_between_q_and_r",
    "always_after_q",
    "always_before_r",
    "always_between_q_and_r",
    "recurrence_globally",
    "recurrence_between_q_and_r",
    "response_globally",
    "response_between_q_and_r",
]


def gen(name, duration):
    _past, future, rows = timescales.generate(
        property=name, lower_bound=LBOUND, upper_bound=UBOUND,
        min_recur=MIN_RECUR, max_recur=MAX_RECUR, duration=duration,
        limit_stutter=0, failing_end=False)
    return future, rows


def write_tuples(rows, path):
    if not rows:
        open(path, "w").close()
        return 0
    fields = list(rows[0].data._fields)
    with open(path, "w") as f:
        f.write("# (time, " + ", ".join(fields) + ")\n")
        for row in rows:
            vals = [str(row.time)] + [str(int(d)) for d in row.data]  # bool -> 0/1
            f.write("(" + ", ".join(vals) + ")\n")
    return len(rows)


def main():
    if len(sys.argv) != 2 or not sys.argv[1].isdigit():
        sys.exit("usage: gen_traces_timescales.py <N>   (N = trace length / duration)")
    duration = int(sys.argv[1])
    os.makedirs(OUT_DIR, exist_ok=True)

    print(f"\n=== Formulas (timescales, future fragment, N={duration}) ===")
    for i, name in enumerate(PROPERTIES, 1):
        future, _ = gen(name, 10)              # cheap pass just for the formula
        print(f"{i:>2}  {name:<28} {future}")
    print()

    rows_out = []
    for name in PROPERTIES:
        out_path = os.path.join(OUT_DIR, name + ".tuples")
        # Time generate + write-to-disk, matching gen_traces.py (whose filewriter
        # subprocess includes serialization + writing the trace file).
        t0 = time.perf_counter()
        _future, rows = gen(name, duration)
        n = write_tuples(rows, out_path)
        dt = time.perf_counter() - t0
        rows_out.append((name, n, dt))
        print(f"  generated {name} ({n} events, {dt * 1000:.1f} ms)")

    print(f"\n=== timescales trace generation (N={duration}, time = generate + write) ===\n")
    hdr = f"{'#':>2}  {'property':<28}{'events':>9}{'time':>12}{'ev/s':>14}"
    print(hdr)
    print("-" * len(hdr))
    tot_events = tot_time = 0.0
    for i, (name, n, dt) in enumerate(rows_out, 1):
        rate = f"{n / dt:,.0f}" if dt > 0 else "-"
        tot_events += n
        tot_time   += dt
        print(f"{i:>2}  {name:<28}{n:>9}{dt * 1000:>10.1f}ms{rate:>14}")
    print("-" * len(hdr))
    overall = f"{tot_events / tot_time:,.0f}" if tot_time > 0 else "-"
    print(f"total: {len(rows_out)} cases, {int(tot_events)} events in {tot_time:.3f}s "
          f"({overall} ev/s)")
    print(f"\ntuples written to {os.path.relpath(OUT_DIR, REPO)}/<name>.tuples")


if __name__ == "__main__":
    main()

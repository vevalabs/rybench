#!/usr/bin/env python3
"""Measure RyBench property-guided generation throughput (generation only).

Runs rybench-filewriter with a *null* serialization backend so that only the
generator is timed — obligation processing + value sampling — with no wire
encoding, no file I/O, and no process-startup cost. The binary reports its own
in-process generate()-only time when RYBENCH_BENCH is set; this script parses
that number, takes the best of several repeats, and prints a table in the same
format as bench_timescales.py for a side-by-side comparison.

    benchmark/mtl/bench_gen.py
    benchmark/mtl/bench_gen.py --durations 1000 100000 1000000 --repeats 7
"""
import argparse
import json
import os
import platform
import re
import subprocess
import sys
import tempfile
from datetime import datetime, timezone

MTL_DIR = os.path.dirname(os.path.abspath(__file__))
BENCH   = os.path.dirname(MTL_DIR)                              # benchmark/
REPO    = os.path.dirname(BENCH)
BINARY  = os.path.join(REPO, "build", "bin", "rybench-filewriter")

# Ad-hoc cases excluded (keep only the Timescales-comparable set).
EXCLUDE = {"eventual_p", "recurrence_demorgan"}

BENCH_RE = re.compile(r"\[bench\] gen-only:\s*(\d+)\s*events,\s*([\d.]+)\s*ms")


def properties():
    return sorted(d for d in os.listdir(MTL_DIR)
                  if os.path.isfile(os.path.join(MTL_DIR, d, "config.toml"))
                  and d not in EXCLUDE)


def override(cfg, count):
    """Force the null generation backend + null output + target count, and
    silence the file output, leaving everything else (schema, formula) intact."""
    cfg = re.sub(r'(?m)^\s*count\s*=.*$',  f'count = {count}',       cfg)
    cfg = re.sub(r'(?m)^\s*output\s*=.*$', 'output = "/dev/null"',   cfg)
    cfg = re.sub(r'(?m)^\s*type\s*=.*$',   'type = "null"',          cfg)  # [serializer]
    if re.search(r'(?m)^\s*backend\s*=', cfg):
        cfg = re.sub(r'(?m)^\s*backend\s*=.*$', 'backend = "null"',  cfg)
    else:
        cfg = re.sub(r'(?m)^\s*\[generator\]\s*$',
                     '[generator]\nbackend = "null"', cfg)
    return cfg


def lookahead_for(name, easy_la, hard_la):
    """before/between scopes create overlapping require-vs-forbid obligations and
    need lookahead = bound+1; globally/after scopes never conflict and generate at
    any depth, so they run at lookahead 1 to avoid paying the lookahead tax."""
    hard = ("before" in name) or ("between" in name)
    return hard_la if hard else easy_la


def bench_one(name, count, repeats, lookahead):
    cfg = open(os.path.join(MTL_DIR, name, "config.toml")).read()
    best_ms, rows = None, 0
    with tempfile.TemporaryDirectory() as tmp:
        node = os.path.join(tmp, name)
        os.makedirs(node)
        open(os.path.join(node, "config.toml"), "w").write(override(cfg, count))
        env = dict(os.environ, RYBENCH_CONFIG=tmp, RYBENCH_BENCH="1",
                   RYBENCH_LOOKAHEAD=str(lookahead))
        for _ in range(repeats):
            p = subprocess.run([BINARY, name], cwd=REPO, env=env,
                               capture_output=True, text=True)
            m = BENCH_RE.search(p.stdout)
            if not m:
                continue
            rows = int(m.group(1))
            ms   = float(m.group(2))
            best_ms = ms if best_ms is None else min(best_ms, ms)
    best_sec = (best_ms / 1000.0) if best_ms else 0.0
    return {"rows": rows, "best_sec": best_sec, "lookahead": lookahead,
            "events_per_sec": rows / best_sec if best_sec > 0 else 0.0}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--durations", type=int, nargs="+",
                    default=[1_000, 10_000, 100_000, 1_000_000])
    ap.add_argument("--repeats", type=int, default=5)
    ap.add_argument("--easy-lookahead", type=int, default=1,
                    help="lookahead for globally/after scopes (no conflicts)")
    ap.add_argument("--hard-lookahead", type=int, default=11,
                    help="lookahead for before/between scopes (bound+1)")
    ap.add_argument("--properties", nargs="+", default=None)
    ap.add_argument("--out",
                    default=os.path.join(BENCH, "results", "rybench_gen_baseline.json"))
    args = ap.parse_args()

    if not os.path.exists(BINARY):
        sys.exit(f"missing {BINARY} — build rybench-filewriter first")

    props = args.properties or properties()

    # (Re)compile the schemas the configs reference.
    for name in props:
        fbs = os.path.join(MTL_DIR, name, "schema.fbs")
        if os.path.exists(fbs):
            subprocess.run(["flatc", "--binary", "--schema", "-o",
                            os.path.join(MTL_DIR, name), fbs], check=True)

    print(f"rybench property-guided throughput  (repeats={args.repeats}, "
          f"best-of, gen only)\n")
    hdr = (f"{'property':<38}{'duration':>10}{'rows':>12}"
           f"{'gen ms':>10}{'gen ev/s':>14}{'la':>5}")
    print(hdr)
    print("-" * len(hdr))

    results = []
    for name in props:
        la = lookahead_for(name, args.easy_lookahead, args.hard_lookahead)
        for d in args.durations:
            r = bench_one(name, d, args.repeats, la)
            r.update(property=name, duration=d)
            results.append(r)
            print(f"{name:<38}{d:>10}{r['rows']:>12}"
                  f"{r['best_sec']*1e3:>10.2f}{r['events_per_sec']:>14,.0f}{la:>5}")
        print()

    baseline = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "python": platform.python_version(),
        "machine": platform.machine(),
        "params": {"repeats": args.repeats, "mode": "gen-only (null backend)"},
        "results": results,
    }
    os.makedirs(os.path.dirname(args.out), exist_ok=True)
    with open(args.out, "w") as f:
        json.dump(baseline, f, indent=2)
    print(f"baseline written: {os.path.relpath(args.out, BENCH)}")


if __name__ == "__main__":
    main()

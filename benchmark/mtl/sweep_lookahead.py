#!/usr/bin/env python3
"""Sweep the probe LOOKAHEAD depth and stack the result tables.

    benchmark/mtl/sweep_lookahead.py [N]

Runs gen_traces.py once per RYBENCH_LOOKAHEAD in {1,4,6,8,11} (default N=10000)
and prints each generation table below the previous one, so the speed/quality
trade-off across lookahead depths is easy to compare.
"""
import os
import subprocess
import sys

MTL_DIR = os.path.dirname(os.path.abspath(__file__))
GEN     = os.path.join(MTL_DIR, "gen_traces.py")
N       = sys.argv[1] if len(sys.argv) > 1 else "10000"
DEPTHS  = [1, 4, 6, 8, 11]


def table_of(text):
    lines = text.splitlines()
    start = next((i for i, l in enumerate(lines)
                  if l.lstrip().startswith("#  property")), None)
    end   = next((i for i, l in enumerate(lines)
                  if l.lstrip().startswith("total:")), None)
    if start is None or end is None:
        return text.strip()
    return "\n".join(lines[start:end + 1])


def main():
    print(f"=== lookahead-depth sweep (N={N}) ===", flush=True)
    for d in DEPTHS:
        print(f"\n################## lookahead = {d} ##################  (running...)",
              flush=True)
        env = dict(os.environ, RYBENCH_LOOKAHEAD=str(d))
        proc = subprocess.run([sys.executable, GEN, N],
                              env=env, capture_output=True, text=True)
        print(table_of(proc.stdout), flush=True)


if __name__ == "__main__":
    main()

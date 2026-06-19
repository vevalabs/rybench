#!/usr/bin/env python3
"""
Generate one property-guided trace per MTL benchmark case and report statistics.

    benchmark/mtl/gen_traces.py <N>

N = trace length (number of events / message count) for every case. For each of
the 12 cases a trace of up to N events is generated and written, as tuples, to
benchmark/mtl/output_traces/<name>.tuples. A statistics table is printed at the end.
"""
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
import time

MTL_DIR = os.path.dirname(os.path.abspath(__file__))
REPO    = os.path.dirname(os.path.dirname(MTL_DIR))
BINARY  = os.path.join(REPO, "build", "bin", "rybench-filewriter")
OUT_DIR = os.path.join(MTL_DIR, "output_traces")


# Ad-hoc cases excluded from the benchmark run (keep only the Timescales set).
EXCLUDE = {"eventual_p", "recurrence_demorgan"}


def properties():
    return sorted(d for d in os.listdir(MTL_DIR)
                  if os.path.isfile(os.path.join(MTL_DIR, d, "config.toml"))
                  and d not in EXCLUDE)


def formula(name):
    fbs = os.path.join(MTL_DIR, name, "schema.fbs")
    if os.path.exists(fbs):
        m = re.search(r'mtl:\s*"([^"]*)"', open(fbs).read())
        if m:
            return m.group(1)
    return "?"


def override(cfg, count, out_path):
    cfg = re.sub(r'(?m)^\s*count\s*=.*$',  f'count = {count}',        cfg)
    cfg = re.sub(r'(?m)^\s*output\s*=.*$', f'output = "{out_path}"',  cfg)
    return cfg


def parse_stats(text):
    m = re.search(
        r"retry stats: steps=(\d+), trials=(\d+), first-try=(\d+) \((\d+)%\), "
        r"retried-steps=(\d+), retries=(\d+), max-trials/step=(\d+)(?:, give-ups=(\d+))?",
        text)
    if not m:
        return None
    g = m.groups()
    pa = re.search(r"proceed-attempts=(\d+)(?: \((\d+)/step\))?", text)
    rc = re.search(r"reroll-cause\[tryStep-fail=(\d+), probe-fail=(\d+)\]", text)
    return {"steps": int(g[0]), "trials": int(g[1]), "first_pct": int(g[3]),
            "retries": int(g[5]), "max_trials": int(g[6]), "give_ups": int(g[7] or 0),
            "proceed": int(pa.group(1)) if pa else 0,
            "proceed_per_step": int(pa.group(2)) if pa and pa.group(2) else 0,
            "tsfail": int(rc.group(1)) if rc else 0,
            "pfail": int(rc.group(2)) if rc else 0}


def read_frames(path):
    """Read the size-prefixed JSON dump: [uint32 little-endian size][json] ..."""
    if not os.path.exists(path):
        return []
    data = open(path, "rb").read()
    events, i = [], 0
    while i + 4 <= len(data):
        size = struct.unpack_from("<I", data, i)[0]
        i += 4
        if i + size > len(data):
            break
        events.append(json.loads(data[i:i + size]))
        i += size
    return events


def write_tuples(events, path):
    if not events:
        open(path, "w").close()
        return 0
    keys = list(events[0].keys())
    if "timestamp" in keys:                       # timestamp first, then the atoms
        keys = ["timestamp"] + [k for k in keys if k != "timestamp"]
    with open(path, "w") as f:
        f.write("# (" + ", ".join(keys) + ")\n")
        for e in events:
            vals = (str(int(e.get(k)) if isinstance(e.get(k), bool) else e.get(k))
                    for k in keys)
            f.write("(" + ", ".join(vals) + ")\n")
    return len(events)


def run_case(name, count):
    cfg = open(os.path.join(MTL_DIR, name, "config.toml")).read()
    with tempfile.TemporaryDirectory() as tmp:
        node_dir = os.path.join(tmp, name)
        os.makedirs(node_dir)
        out_file = os.path.join(tmp, name + ".out")
        open(os.path.join(node_dir, "config.toml"), "w").write(
            override(cfg, count, out_file))
        env = dict(os.environ, RYBENCH_CONFIG=tmp)
        t0 = time.perf_counter()
        proc = subprocess.run([BINARY, name], cwd=REPO, env=env,
                              capture_output=True, text=True)
        elapsed = time.perf_counter() - t0
        log = proc.stdout + proc.stderr
        events = read_frames(out_file)
    n = write_tuples(events, os.path.join(OUT_DIR, name + ".tuples"))
    return n, parse_stats(log), elapsed


def status(n, count, st):
    if st and st["give_ups"]:
        return "gave-up"
    if n >= count:
        return "ok"
    return "finite"          # bounded formula satisfied before reaching N


def main():
    if len(sys.argv) != 2 or not sys.argv[1].isdigit():
        sys.exit("usage: gen_traces.py <N>   (N = trace length / events per case)")
    count = int(sys.argv[1])
    if not os.path.exists(BINARY):
        sys.exit(f"missing {BINARY} — build rybench-filewriter first")
    os.makedirs(OUT_DIR, exist_ok=True)

    # (Re)compile schemas the configs reference.
    for name in properties():
        fbs = os.path.join(MTL_DIR, name, "schema.fbs")
        if os.path.exists(fbs):
            subprocess.run(["flatc", "--binary", "--schema", "-o",
                            os.path.join(MTL_DIR, name), fbs], check=True)

    print(f"\n=== Formulas (N={count}) ===")
    for i, name in enumerate(properties(), 1):
        print(f"{i:>2}  {name:<38} {formula(name)}")
    print()

    rows = []
    for name in properties():
        n, st, dt = run_case(name, count)
        rows.append((name, n, st, dt))
        print(f"  generated {name} ({n} events, {dt * 1000:.1f} ms)")

    print(f"\n=== Property-Guided trace generation (target length N={count}) ===\n")
    hdr = (f"{'#':>2}  {'property':<38}{'events':>7}{'first-try':>10}"
           f"{'retries':>9}{'give-ups':>9}{'time':>11}{'ev/s':>12}{'proc/step':>10}"
           f"{'tsf':>8}{'pf':>8}  status")
    print(hdr)
    print("-" * len(hdr))
    tot_events = ok = finite = gave = 0
    tot_time = 0.0
    for i, (name, n, st, dt) in enumerate(rows, 1):
        ft = f"{st['first_pct']}%" if st else "-"
        rt = st["retries"]  if st else "-"
        gu = st["give_ups"] if st else "-"
        pps = st["proceed_per_step"] if st else "-"
        tsf = st["tsfail"] if st else "-"
        pf  = st["pfail"]  if st else "-"
        s = status(n, count, st)
        rate = f"{n / dt:,.0f}" if dt > 0 else "-"
        tot_events += n
        tot_time   += dt
        ok     += s == "ok"
        finite += s == "finite"
        gave   += s == "gave-up"
        print(f"{i:>2}  {name:<38}{n:>7}{ft:>10}{rt:>9}{gu:>9}"
              f"{dt * 1000:>9.1f}ms{rate:>12}{pps:>10}{tsf:>8}{pf:>8}  {s}")
    print("-" * len(hdr))
    overall = f"{tot_events / tot_time:,.0f}" if tot_time > 0 else "-"
    print(f"total: {len(rows)} cases, {tot_events} events in {tot_time:.3f}s "
          f"({overall} ev/s)  |  ok={ok}  finite={finite}  gave-up={gave}")
    print(f"\ntuples written to {os.path.relpath(OUT_DIR, REPO)}/<name>.tuples")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Table 4 -- Reelay monitoring throughput by serialization format (offline replay).

For an MTL property, the same trace is pre-serialized to json / flatbuffers / protobuf
dumps, then each is decoded and fed to the SAME Reelay past-time monitor
(rybench-monitor). The monitor step is identical across formats, so the difference is
the per-message DECODE cost. Reports avg message size (B) and monitoring throughput
(events/s). Offline: generation is not timed; the dump is replayed from memory.
"""
import argparse, os, re, struct, subprocess, tempfile, time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN  = os.path.join(REPO, "build", "bin")
FW   = os.path.join(BIN, "rybench-filewriter")
MON  = os.path.join(BIN, "rybench-monitor")
RYJSON = os.path.join(REPO, "rv-tools", "reelay", "build", "apps", "ryjson1", "ryjson")

MTL = os.path.join(REPO, "benchmark", "mtl")

# The 10 Timescales properties. The reelay past-spec is derived per property by
# mirroring its schema.fbs future formula (bounds preserved) — same source the
# generator targets, so no guard-bound mismatch.
PROP_STEMS = [
    "bounded_absence_after_q",           "bounded_absence_before_r",
    "bounded_absence_between_q_and_r",   "bounded_universality_after_q",
    "bounded_universality_before_r",     "bounded_universality_between_q_and_r",
    "bounded_recurrence_globally",       "bounded_recurrence_between_q_and_r",
    "bounded_response_globally",         "bounded_response_between_q_and_r",
]
SERIALIZERS = ["json", "flatbuffers", "protobuf"]

def schema_mtl(stem):
    m = re.search(r'mtl:\s*"([^"]*)"', open(os.path.join(MTL, stem, "schema.fbs")).read())
    return m.group(1) if m else None

def mirror(future):
    """Mirror a future MTL formula to reelay past syntax (bounds preserved)."""
    s = future.replace("[0,inf]", "")
    s = re.sub(r'\[\s*(\d+)\s*,\s*(\d+)\s*\]', r'[\1:\2]', s)
    s = re.sub(r'\balways\b', 'historically', s)
    s = re.sub(r'\beventually\b', 'once', s)
    s = re.sub(r'\buntil\b', 'since', s)
    s = re.sub(r'\bimplies\b', '->', s)
    s = re.sub(r'(?<![\w{])([pqrs])(?![\w}])', r'{\1}', s)
    return re.sub(r'\s+', ' ', s).strip()

def build_props(count):
    props = []
    for stem in PROP_STEMS:
        fut = schema_mtl(stem)
        if not fut:
            continue
        props.append((stem[len("bounded_"):],
                      f"benchmark/mtl/{stem}/schema.bfbs",
                      mirror(fut), count))
    return props

def backend_for(ser):
    return ser if ser in ("protobuf", "flatbuffers") else "flatbuffers"

def make_dump(bfbs, ser, count, path):
    cfg = (f'[runtime]\napp_name = "n"\n[schema]\nbfbs = "{bfbs}"\n'
           f'[generator]\nalgorithm = "property_guided"\nbackend = "{backend_for(ser)}"\n'
           f'seed = 1\ncount = {count}\n'
           f'[serializer]\ntype = "{ser}"\n'
           f'[filewriter]\noutput = "{path}"\nrate_hz = 0\n')
    with tempfile.TemporaryDirectory() as tmp:
        node = os.path.join(tmp, "n"); os.makedirs(node)
        open(os.path.join(node, "config.toml"), "w").write(cfg)
        subprocess.run([FW, "n"], cwd=REPO, env=dict(os.environ, RYBENCH_CONFIG=tmp),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=300)
    return (os.path.getsize(path) - 4 * count) / count if os.path.exists(path) else 0.0

def monitor(ser, bfbs, dump, spec, repeats):
    best = 0.0
    for _ in range(repeats):
        out = subprocess.run([MON, ser, bfbs, dump, spec], cwd=REPO,
                             capture_output=True, text=True, timeout=300).stdout
        m = re.search(r"events/s=(\d+)", out)
        if m:
            best = max(best, int(m.group(1)))
    return best

def dump_to_jsonl(dump, jsonl):
    """Strip the 4-byte size prefixes from a json dump -> one JSON object per line
    (the format reelay's native ryjson consumes via simdjson load_many)."""
    data = open(dump, "rb").read()
    i = n = 0
    with open(jsonl, "w") as out:
        while i + 4 <= len(data):
            sz = struct.unpack_from("<I", data, i)[0]; i += 4
            if i + sz > len(data): break
            out.write(data[i:i + sz].decode() + "\n"); i += sz; n += 1
    return n

def ryjson_run(jsonl, spec, count, repeats):
    """Reelay's native JSON path (simdjson). Wall-time of the tool over the JSONL;
    includes process start + its condensed .ryl output write (kept in tmp)."""
    best = 0.0
    for _ in range(repeats):
        t0 = time.perf_counter()
        subprocess.run([RYJSON, "-x", "-b", spec, "--tname", "timestamp", jsonl],
                       cwd=REPO, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                       timeout=300)
        dt = time.perf_counter() - t0
        if dt > 0:
            best = max(best, count / dt)
    return best

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--count", type=int, default=200_000, help="events per property")
    ap.add_argument("--only", default="", help="substring filter on property name")
    args = ap.parse_args()
    for b in (FW, MON):
        if not os.path.exists(b):
            raise SystemExit(f"missing {b} — build it first")

    props = [p for p in build_props(args.count) if args.only in p[0]]
    print("Table 4 -- Reelay monitoring throughput by format (offline replay)\n")
    for label, bfbs, spec, count in props:
        print(f"property: {label}   spec: {spec}   events: {count:,}")
        hdr = f"  {'format':<22}{'avg B':>9}{'events/s':>14}{'Mev/s':>9}"
        print(hdr); print("  " + "-" * (len(hdr) - 2))
        abs_bfbs = os.path.join(REPO, bfbs) if not os.path.isabs(bfbs) else bfbs
        with tempfile.TemporaryDirectory() as tmp:
            json_dump = None
            for ser in SERIALIZERS:
                dump = os.path.join(tmp, f"d_{ser}.bin")
                avg = make_dump(abs_bfbs, ser, count, dump)
                if ser == "json":
                    json_dump = (dump, avg)
                eps = monitor(ser, bfbs, dump, spec, args.repeats)
                print(f"  {ser+' (harness)':<22}{avg:>9.1f}{eps:>14,}{eps/1e6:>9.2f}")
            # 4th row: reelay's own JSON path (ryjson / simdjson) over the same events
            if json_dump and os.path.exists(RYJSON):
                jsonl = os.path.join(tmp, "t.jsonl")
                n = dump_to_jsonl(json_dump[0], jsonl)
                eps = ryjson_run(jsonl, spec, n, args.repeats)
                print(f"  {'json (ryjson)':<22}{json_dump[1]:>9.1f}{eps:>14,.0f}{eps/1e6:>9.2f}")
        print()

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Table 3 -- MTL generation through every serializer x transport, per property.

The property_guided generator runs the full pipeline (generate + serialize +
transport) for a fast property (Bounded Response Globally, lookahead 1) and a slow
one (Bounded Absence Before r, lookahead 11). Rows are serializer x transport; the
two properties sit side by side. Shows that the serializer/transport matter on the
fast property (generation cheap) but wash out on the slow one (generation dominates).
"""
import argparse, os, subprocess, tempfile, time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN  = os.path.join(REPO, "build", "bin")

# label, mtl schema, lookahead, count
PROPS = [("fast (Response Globally)", "benchmark/mtl/bounded_response_globally/schema.bfbs", 1,  1_000_000),
         ("slow (Absence Before r)",  "benchmark/mtl/bounded_absence_before_r/schema.bfbs", 11,  200_000)]
SERIALIZERS = ["json", "flatbuffers", "protobuf"]
TRANSPORTS = [
    ("filewriter", "rybench-filewriter",
     '[filewriter]\noutput = "/dev/null"\nrate_hz = 10000000\n'),
    ("iceoryx2", "rybench-iceoryx2",
     '[iceoryx2]\nmode = "publish"\nservice = "Bench"\ninstance = "Default"\n'
     'event = "Data"\nmax_payload_size = 4096\nrate_hz = 10000000\n'),
    ("zenoh", "rybench-zenoh",
     '[zenoh]\nmode = "publish"\nkey_expr = "Bench/Default/Data"\n'
     'max_payload_size = 4096\nrate_hz = 10000000\n'),
]

# Match generation backend to the output serializer so protobuf builds directly
# (no flat->proto convert); json has no direct builder and falls back to flatbuffers.
def backend_for(ser):
    return ser if ser in ("protobuf", "flatbuffers") else "flatbuffers"

def config(bfbs, ser, count, transport_block):
    return (f'[runtime]\napp_name = "n"\n[schema]\nbfbs = "{bfbs}"\n'
            f'[generator]\nalgorithm = "property_guided"\nbackend = "{backend_for(ser)}"\n'
            f'seed = 1\ncount = {count}\n'
            f'[serializer]\ntype = "{ser}"\n{transport_block}')

def run(app, cfg_text, la):
    with tempfile.TemporaryDirectory() as tmp:
        node = os.path.join(tmp, "n"); os.makedirs(node)
        open(os.path.join(node, "config.toml"), "w").write(cfg_text)
        env = dict(os.environ, RYBENCH_CONFIG=tmp, RYBENCH_LOOKAHEAD=str(la))
        t0 = time.perf_counter()
        subprocess.run([os.path.join(BIN, app), "n"], cwd=REPO, timeout=300, env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return time.perf_counter() - t0

def avg_size(bfbs, ser, la, n=2000):
    with tempfile.TemporaryDirectory() as tmp:
        f = os.path.join(tmp, "o")
        run("rybench-filewriter", config(bfbs, ser, n, f'[filewriter]\noutput = "{f}"\nrate_hz = 10000000\n'), la)
        return os.path.getsize(f) / n if os.path.exists(f) else 0.0

def cell(mps, avg):
    return f"{avg:>9.1f}{mps:>12,.0f}{mps*avg/1e6:>9.1f}"

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--repeats", type=int, default=2)
    args = ap.parse_args()

    avg = {(ser, bfbs): avg_size(bfbs, ser, la)
           for ser in SERIALIZERS for _, bfbs, la, _ in PROPS}

    print("Table 3 -- MTL generation (avg B, msg/s, MB/s)\n")
    hdr = f"{'serializer':<12}{'transport':<11}"
    for label, _, _, _ in PROPS:
        hdr += f"|{label:^30}"
    print(hdr)
    print(f"{'':<23}" + "|" + f"{'avg B':>7}{'msg/s':>12}{'MB/s':>9}   " * len(PROPS))
    print("-" * len(hdr))
    for ser in SERIALIZERS:
        for tname, app, tblock in TRANSPORTS:
            row = f"{ser:<12}{tname:<11}"
            for _, bfbs, la, count in PROPS:
                best = min(run(app, config(bfbs, ser, count, tblock), la) for _ in range(args.repeats))
                mps = count / best if best > 0 else 0
                row += "|" + cell(mps, avg[(ser, bfbs)]) + "   "
            print(row)
        print()

if __name__ == "__main__":
    main()

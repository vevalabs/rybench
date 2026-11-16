#!/usr/bin/env python3
"""Table 1 -- random generation through every serializer x transport, per schema.

Rows are serializer x transport; the two OSI schemas sit side by side. Each cell
reports avg message size (B), throughput (msg/s) and bandwidth (MB/s). The random
(schema_guided) generator runs the full pipeline: generate + serialize + transport.
filewriter writes to /dev/null; iceoryx2/zenoh publish (publish side only).
"""
import argparse, os, subprocess, tempfile, time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN  = os.path.join(REPO, "build", "bin")

SCHEMAS = [("small (VehicleMotion)",   "schemas/osi_vehiclemotion.bfbs",  1_000_000),
           ("large (HostVehicleData)", "schemas/osi_hostvehicledata.bfbs",  200_000)]
SERIALIZERS = ["json", "flatbuffers", "protobuf"]
TRANSPORTS = [
    ("filewriter", "rybench-filewriter",
     '[filewriter]\noutput = "/dev/null"\nrate_hz = 10000000\n'),
    ("iceoryx2", "rybench-iceoryx2",
     '[iceoryx2]\nmode = "publish"\nservice = "Bench"\ninstance = "Default"\n'
     'event = "Data"\nmax_payload_size = 32768\nrate_hz = 10000000\n'),
    ("zenoh", "rybench-zenoh",
     '[zenoh]\nmode = "publish"\nkey_expr = "Bench/Default/Data"\n'
     'max_payload_size = 32768\nrate_hz = 10000000\n'),
]

# Generation backend = the format the generator builds directly. Match it to the
# output serializer so protobuf uses its direct DynamicMessage builder (no flat->proto
# convert). json has no direct builder, so it falls back to flatbuffers + convert.
def backend_for(ser):
    return ser if ser in ("protobuf", "flatbuffers") else "flatbuffers"

def gen_block(ser, count):
    return (f'[generator]\nalgorithm = "schema_guided"\nbackend = "{backend_for(ser)}"\n'
            f'seed = 1\ncount = {count}\n')

def config(bfbs, ser, count, transport_block):
    return (f'[runtime]\napp_name = "n"\n[schema]\nbfbs = "{bfbs}"\n'
            f'{gen_block(ser, count)}[serializer]\ntype = "{ser}"\n{transport_block}')

def run(app, cfg_text):
    with tempfile.TemporaryDirectory() as tmp:
        node = os.path.join(tmp, "n"); os.makedirs(node)
        open(os.path.join(node, "config.toml"), "w").write(cfg_text)
        t0 = time.perf_counter()
        subprocess.run([os.path.join(BIN, app), "n"], cwd=REPO, timeout=300,
                       env=dict(os.environ, RYBENCH_CONFIG=tmp),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return time.perf_counter() - t0

def avg_size(bfbs, ser, n=2000):
    with tempfile.TemporaryDirectory() as tmp:
        f = os.path.join(tmp, "o")
        run("rybench-filewriter", config(bfbs, ser, n, f'[filewriter]\noutput = "{f}"\nrate_hz = 10000000\n'))
        return os.path.getsize(f) / n if os.path.exists(f) else 0.0

def cell(mps, avg):
    return f"{avg:>9.1f}{mps:>12,.0f}{mps*avg/1e6:>9.1f}"

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--repeats", type=int, default=2)
    args = ap.parse_args()

    avg = {(ser, bfbs): avg_size(bfbs, ser) for ser in SERIALIZERS for _, bfbs, _ in SCHEMAS}

    print("Table 1 -- random generation (avg B, msg/s, MB/s)\n")
    hdr = f"{'serializer':<12}{'transport':<11}"
    for label, _, _ in SCHEMAS:
        hdr += f"|{label:^28}"
    print(hdr)
    print(f"{'':<23}" + "|" + f"{'avg B':>7}{'msg/s':>12}{'MB/s':>9}" * len(SCHEMAS))
    print("-" * len(hdr))
    for ser in SERIALIZERS:
        for tname, app, tblock in TRANSPORTS:
            row = f"{ser:<12}{tname:<11}"
            for _, bfbs, count in SCHEMAS:
                best = min(run(app, config(bfbs, ser, count, tblock)) for _ in range(args.repeats))
                mps = count / best if best > 0 else 0
                row += "|" + cell(mps, avg[(ser, bfbs)])
            print(row)
        print()

if __name__ == "__main__":
    main()

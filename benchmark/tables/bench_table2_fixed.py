#!/usr/bin/env python3
"""Table 2 -- fixed replay (transport ceiling) through the IPC transports.

The fixed generator replays a pre-serialized dump straight to the transport, so
generation is ~free and no re-serialization happens (pass-through). Each "serializer"
row is therefore a dump PRODUCED with that format -- i.e. a different payload size,
not a live encode. Rows are serializer(=dump format) x transport (iceoryx2, zenoh);
the two OSI schemas sit side by side. filewriter is excluded (disk sink, not an IPC
transport). Publish side only.
"""
import argparse, os, subprocess, tempfile, time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN  = os.path.join(REPO, "build", "bin")

SCHEMAS = [("small (VehicleMotion)",   "schemas/osi_vehiclemotion.bfbs",  2_000_000),
           ("large (HostVehicleData)", "schemas/osi_hostvehicledata.bfbs",  500_000)]
SERIALIZERS = ["json", "flatbuffers", "protobuf"]
TRANSPORTS = [
    ("iceoryx2", "rybench-iceoryx2",
     '[iceoryx2]\nmode = "publish"\nservice = "Bench"\ninstance = "Default"\n'
     'event = "Data"\nmax_payload_size = 32768\nrate_hz = 10000000\n'),
    ("zenoh", "rybench-zenoh",
     '[zenoh]\nmode = "publish"\nkey_expr = "Bench/Default/Data"\n'
     'max_payload_size = 32768\nrate_hz = 10000000\n'),
]

def run(app, cfg_text):
    with tempfile.TemporaryDirectory() as tmp:
        node = os.path.join(tmp, "n"); os.makedirs(node)
        open(os.path.join(node, "config.toml"), "w").write(cfg_text)
        t0 = time.perf_counter()
        subprocess.run([os.path.join(BIN, app), "n"], cwd=REPO, timeout=300,
                       env=dict(os.environ, RYBENCH_CONFIG=tmp),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return time.perf_counter() - t0

def make_dump(bfbs, ser, path, n=1000):
    """Random-generate n messages serialized with `ser`, dumped for replay."""
    cfg = (f'[runtime]\napp_name = "n"\n[schema]\nbfbs = "{bfbs}"\n'
           f'[generator]\nalgorithm = "schema_guided"\nseed = 1\ncount = {n}\n'
           f'[serializer]\ntype = "{ser}"\n[filewriter]\noutput = "{path}"\nrate_hz = 10000000\n')
    run("rybench-filewriter", cfg)
    return (os.path.getsize(path) - 4 * n) / n if os.path.exists(path) else 0.0  # minus 4B length prefix

def replay_cfg(bfbs, dump, count, transport_block):
    return (f'[runtime]\napp_name = "n"\n[schema]\nbfbs = "{bfbs}"\n'
            f'[generator]\nalgorithm = "fixed"\nseed = 1\ncount = {count}\nreplay = "{dump}"\n'
            f'[serializer]\ntype = "flatbuffers"\n{transport_block}')  # flatbuffers = pass-through

def cell(mps, avg):
    return f"{avg:>9.1f}{mps:>12,.0f}{mps*avg/1e6:>9.1f}"

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--repeats", type=int, default=2)
    args = ap.parse_args()

    dumps, avg = {}, {}
    with tempfile.TemporaryDirectory() as dtmp:
        for ser in SERIALIZERS:
            for _, bfbs, _ in SCHEMAS:
                p = os.path.join(dtmp, f"{ser}_{os.path.basename(bfbs)}.dump")
                avg[(ser, bfbs)] = make_dump(bfbs, ser, p)
                dumps[(ser, bfbs)] = p

        print("Table 2 -- fixed replay / transport ceiling (avg B, msg/s, MB/s)\n")
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
                    best = min(run(app, replay_cfg(bfbs, dumps[(ser, bfbs)], count, tblock))
                               for _ in range(args.repeats))
                    mps = count / best if best > 0 else 0
                    row += "|" + cell(mps, avg[(ser, bfbs)])
                print(row)
            print()

if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""End-to-end generation+serialization throughput for the perf tables.

For each (schema, serializer) we run the random (schema_guided) generator through
rybench-filewriter to /dev/null and time the whole run: generation + serialize +
sink. Message size (bytes) is measured from a separate short run to a real file,
since messages vary in size. Throughput is best-of-N wall-clock.

    benchmark/perf/bench_serialize.py
    benchmark/perf/bench_serialize.py --repeats 5

Transport is the filewriter (cheap sink) so this isolates gen+serialize; the IPC
transports (iceoryx2/zenoh) are measured separately.
"""
import argparse, os, subprocess, tempfile, time

REPO   = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BINARY = os.path.join(REPO, "build", "bin", "rybench-filewriter")

SCHEMAS = [("small (VehicleMotion)", "schemas/osi_vehiclemotion.bfbs", 1_000_000),
           ("large (HostVehicleData)", "schemas/osi_hostvehicledata.bfbs", 200_000)]
SERIALIZERS = ["json", "flatbuffers", "protobuf"]


def cfg(bfbs, ser, count, out):
    return (f'[runtime]\napp_name = "bench"\n[schema]\nbfbs = "{bfbs}"\n'
            f'[generator]\nalgorithm = "schema_guided"\nseed = 1\ncount = {count}\n'
            f'[serializer]\ntype = "{ser}"\n'
            f'[filewriter]\noutput = "{out}"\nrate_hz = 10000000\n')


def run(bfbs, ser, count, out):
    with tempfile.TemporaryDirectory() as tmp:
        node = os.path.join(tmp, "bench"); os.makedirs(node)
        open(os.path.join(node, "config.toml"), "w").write(cfg(bfbs, ser, count, out))
        env = dict(os.environ, RYBENCH_CONFIG=tmp)
        t0 = time.perf_counter()
        subprocess.run([BINARY, "bench"], cwd=REPO, env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return time.perf_counter() - t0


def avg_size(bfbs, ser, n=2000):
    with tempfile.TemporaryDirectory() as tmp:
        f = os.path.join(tmp, "out.bin")
        run(bfbs, ser, n, f)
        return os.path.getsize(f) / n if os.path.exists(f) else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repeats", type=int, default=3)
    args = ap.parse_args()
    if not os.path.exists(BINARY):
        raise SystemExit(f"missing {BINARY} — build rybench-filewriter first")

    for label, bfbs, count in SCHEMAS:
        print(f"\n=== {label}  ({bfbs})   count={count:,} ===")
        print(f"{'serializer':<12}{'avg B':>9}{'msg/s':>14}{'MB/s':>10}")
        print("-" * 45)
        for ser in SERIALIZERS:
            sz = avg_size(bfbs, ser)
            best = min(run(bfbs, ser, count, "/dev/null") for _ in range(args.repeats))
            mps  = count / best if best > 0 else 0
            mbps = mps * sz / 1e6
            print(f"{ser:<12}{sz:>9.0f}{mps:>14,.0f}{mbps:>10.1f}")


if __name__ == "__main__":
    main()

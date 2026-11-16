#!/usr/bin/env python3
"""Transport ceiling: fixed replay pushed through each transport.

The fixed (replay) generator hands pre-serialized flatbuffer messages straight to
the transport, so generation is ~free and serialization is a pass-through. What
we measure is the transport's publish throughput. A dump is created once from the
large schema if missing.

    benchmark/perf/bench_transport.py
    benchmark/perf/bench_transport.py --count 2000000 --repeats 3

Caveats: publish side only (no subscriber); the replay repeats its last message
after the dump is exhausted, so all messages are the same size.
"""
import argparse, os, subprocess, tempfile, time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN  = os.path.join(REPO, "build", "bin")
DUMP = "/tmp/rybench-hvd-flat.dump"
BFBS = "schemas/osi_hostvehicledata.bfbs"

# app binary, config [transport] section
TRANSPORTS = [
    ("filewriter", "rybench-filewriter",
     '[filewriter]\noutput = "/dev/null"\nrate_hz = 10000000\n'),
    ("iceoryx2", "rybench-iceoryx2",
     '[iceoryx2]\nmode = "publish"\nservice = "Benchmark"\ninstance = "Default"\n'
     'event = "Data"\nmax_payload_size = 16384\nrate_hz = 10000000\n'),
    ("zenoh", "rybench-zenoh",
     '[zenoh]\nmode = "publish"\nkey_expr = "Benchmark/Default/Data"\n'
     'max_payload_size = 16384\nrate_hz = 10000000\n'),
]


def write_cfg(node_dir, algo_block, transport_block):
    open(os.path.join(node_dir, "config.toml"), "w").write(
        f'[runtime]\napp_name = "n"\n[schema]\nbfbs = "{BFBS}"\n'
        f'{algo_block}[serializer]\ntype = "flatbuffers"\n{transport_block}')


def make_dump(n=1000):
    if os.path.exists(DUMP):
        return
    with tempfile.TemporaryDirectory() as tmp:
        node = os.path.join(tmp, "n"); os.makedirs(node)
        write_cfg(node, f'[generator]\nalgorithm = "schema_guided"\nseed = 1\ncount = {n}\n',
                  f'[filewriter]\noutput = "{DUMP}"\nrate_hz = 10000000\n')
        subprocess.run([os.path.join(BIN, "rybench-filewriter"), "n"], cwd=REPO,
                       env=dict(os.environ, RYBENCH_CONFIG=tmp),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


def run(app, transport_block, count):
    with tempfile.TemporaryDirectory() as tmp:
        node = os.path.join(tmp, "n"); os.makedirs(node)
        write_cfg(node, f'[generator]\nalgorithm = "fixed"\nseed = 1\ncount = {count}\n'
                        f'replay = "{DUMP}"\n', transport_block)
        t0 = time.perf_counter()
        subprocess.run([os.path.join(BIN, app), "n"], cwd=REPO, timeout=120,
                       env=dict(os.environ, RYBENCH_CONFIG=tmp),
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return time.perf_counter() - t0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--count", type=int, default=2_000_000)
    ap.add_argument("--repeats", type=int, default=3)
    args = ap.parse_args()
    make_dump()
    avg = (os.path.getsize(DUMP) - 4 * 1000) / 1000  # 4-byte length prefix per msg

    print(f"transport ceiling  (fixed replay, {BFBS}, avg {avg:.0f} B, count={args.count:,})")
    print(f"{'transport':<12}{'msg/s':>14}{'MB/s':>10}")
    print("-" * 36)
    for name, app, block in TRANSPORTS:
        if not os.path.exists(os.path.join(BIN, app)):
            print(f"{name:<12}{'(not built)':>14}"); continue
        best = min(run(app, block, args.count) for _ in range(args.repeats))
        mps = args.count / best if best > 0 else 0
        print(f"{name:<12}{mps:>14,.0f}{mps*avg/1e6:>10.1f}")


if __name__ == "__main__":
    main()

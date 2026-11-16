#!/usr/bin/env python3
"""MTL generation + serialization throughput for a fast and a slow property.

Shows how the serializer choice matters only when generation is cheap: on the
fast property (lookahead 1) serialize dominates and the formats spread out; on
the slow property (lookahead 11, heavy reroll) generation dominates and the
format barely moves the number. Uses the property_guided generator through
rybench-filewriter to /dev/null.

    benchmark/perf/bench_mtl_serialize.py
    benchmark/perf/bench_mtl_serialize.py --repeats 5
"""
import argparse, os, re, subprocess, tempfile, time

REPO   = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BINARY = os.path.join(REPO, "build", "bin", "rybench-filewriter")
MTL    = os.path.join(REPO, "benchmark", "mtl")

# label, config dir name, lookahead, count
PROPS = [("fast (globally)",  "bounded_response_globally", 1,  1_000_000),
         ("slow (before_r)",  "bounded_absence_before_r", 11,   200_000)]
SERIALIZERS = ["json", "flatbuffers", "protobuf"]


def cfg(name, ser, count, out):
    src = open(os.path.join(MTL, name, "config.toml")).read()
    src = re.sub(r'(?m)^\s*count\s*=.*$',  f'count = {count}', src)
    src = re.sub(r'(?m)^\s*type\s*=.*$',   f'type = "{ser}"',  src)
    src = re.sub(r'(?m)^\s*output\s*=.*$', f'output = "{out}"', src)
    return src


def run(name, ser, la, count, out):
    with tempfile.TemporaryDirectory() as tmp:
        node = os.path.join(tmp, name); os.makedirs(node)
        open(os.path.join(node, "config.toml"), "w").write(cfg(name, ser, count, out))
        env = dict(os.environ, RYBENCH_CONFIG=tmp, RYBENCH_LOOKAHEAD=str(la))
        t0 = time.perf_counter()
        subprocess.run([BINARY, name], cwd=REPO, env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        return time.perf_counter() - t0


def avg_size(name, ser, la, n=2000):
    with tempfile.TemporaryDirectory() as tmp:
        f = os.path.join(tmp, "o")
        run(name, ser, la, n, f)
        return os.path.getsize(f) / n if os.path.exists(f) else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repeats", type=int, default=3)
    args = ap.parse_args()
    if not os.path.exists(BINARY):
        raise SystemExit(f"missing {BINARY} — build rybench-filewriter first")

    for label, name, la, count in PROPS:
        print(f"\n=== {label}   la={la}  count={count:,} ===")
        print(f"{'serializer':<12}{'avg B':>9}{'msg/s':>14}")
        print("-" * 35)
        for ser in SERIALIZERS:
            sz   = avg_size(name, ser, la)
            best = min(run(name, ser, la, count, "/dev/null") for _ in range(args.repeats))
            print(f"{ser:<12}{sz:>9.0f}{count/best if best else 0:>14,.0f}")


if __name__ == "__main__":
    main()

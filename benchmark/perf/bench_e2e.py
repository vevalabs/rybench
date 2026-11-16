#!/usr/bin/env python3
"""Publish rate WITH a live subscriber -- shared memory (iceoryx2) vs network (zenoh).

We still measure the PUBLISHER's rate (how fast it can publish), but with a real
subscriber connected so the publish path is not a no-op. This matters for the
network transport: without a destination, zenoh's put() has nothing to send; with
a subscriber it does the real encode + send. iceoryx2 copies into shared memory
either way. The subscriber's own receive rate is not measured here.

    benchmark/perf/bench_e2e.py            # small=1M, large=200k
"""
import argparse, os, signal, subprocess, tempfile, time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN  = os.path.join(REPO, "build", "bin")

SCHEMAS = [("small (VehicleMotion)",   "schemas/osi_vehiclemotion.bfbs", 1_000_000),
           ("large (HostVehicleData)", "schemas/osi_hostvehicledata.bfbs", 200_000)]

ICE_PUB = ('[iceoryx2]\nmode = "publish"\nservice = "E2E"\ninstance = "Default"\n'
           'event = "Data"\nmax_payload_size = 32768\nrate_hz = 10000000\n')
ICE_SUB = ('[iceoryx2]\nmode = "consume"\nservice = "E2E"\ninstance = "Default"\n'
           'event = "Data"\nmax_payload_size = 32768\nrate_hz = 10000000\noutput = "/dev/null"\n')
ZEN_PUB = ('[zenoh]\nmode = "publish"\nkey_expr = "E2E/Default/Data"\n'
           'max_payload_size = 32768\nrate_hz = 10000000\n')
ZEN_SUB = ('[zenoh]\nmode = "consume"\nkey_expr = "E2E/Default/Data"\n'
           'max_payload_size = 32768\nrate_hz = 10000000\noutput = "/dev/null"\n')

TRANSPORTS = [("iceoryx2 (shared mem)", "rybench-iceoryx2", ICE_PUB, ICE_SUB),
              ("zenoh (network)",       "rybench-zenoh",    ZEN_PUB, ZEN_SUB)]

def cfg_dir(bfbs, gen_block, transport_block):
    d = tempfile.mkdtemp(); os.makedirs(os.path.join(d, "n"))
    open(os.path.join(d, "n", "config.toml"), "w").write(
        f'[runtime]\napp_name = "n"\n[schema]\nbfbs = "{bfbs}"\n'
        f'{gen_block}[serializer]\ntype = "flatbuffers"\n{transport_block}')
    return d

def make_dump(bfbs, path, n=1000):
    d = cfg_dir(bfbs, f'[generator]\nalgorithm = "schema_guided"\nseed = 1\ncount = {n}\n',
                f'[filewriter]\noutput = "{path}"\nrate_hz = 10000000\n')
    subprocess.run([os.path.join(BIN, "rybench-filewriter"), "n"], cwd=REPO,
                   env=dict(os.environ, RYBENCH_CONFIG=d), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return (os.path.getsize(path) - 4 * n) / n

def pub_rate(app, bfbs, dump, pub_block, sub_block, count):
    subd = cfg_dir(bfbs, '[generator]\nalgorithm = "fixed"\nseed = 1\ncount = 0\nreplay = "/dev/null"\n', sub_block)
    pubd = cfg_dir(bfbs, f'[generator]\nalgorithm = "fixed"\nseed = 1\ncount = {count}\nreplay = "{dump}"\n', pub_block)
    sub = subprocess.Popen([os.path.join(BIN, app), "n"], cwd=REPO,
                           env=dict(os.environ, RYBENCH_CONFIG=subd),
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)                                       # let the subscriber connect
    t0 = time.perf_counter()
    subprocess.run([os.path.join(BIN, app), "n"], cwd=REPO, timeout=300,
                   env=dict(os.environ, RYBENCH_CONFIG=pubd),
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    dt = time.perf_counter() - t0
    sub.send_signal(signal.SIGINT)
    try: sub.wait(timeout=10)
    except subprocess.TimeoutExpired: sub.kill()
    return count / dt if dt > 0 else 0

def main():
    ap = argparse.ArgumentParser(); ap.add_argument("--repeats", type=int, default=2)
    args = ap.parse_args()
    tmp = tempfile.mkdtemp(); dumps, avg = {}, {}
    for _, bfbs, _ in SCHEMAS:
        p = os.path.join(tmp, os.path.basename(bfbs) + ".dump")
        avg[bfbs] = make_dump(bfbs, p); dumps[bfbs] = p

    print("Publish rate with a live subscriber (avg B, msg/s, MB/s)\n")
    hdr = f"{'transport':<22}"
    for label, _, _ in SCHEMAS: hdr += f"|{label:^28}"
    print(hdr)
    print(f"{'':<22}" + "|" + f"{'avg B':>9}{'msg/s':>12}{'MB/s':>9}" * len(SCHEMAS))
    print("-" * len(hdr))
    for tname, app, pub_b, sub_b in TRANSPORTS:
        row = f"{tname:<22}"
        for _, bfbs, count in SCHEMAS:
            mps = max(pub_rate(app, bfbs, dumps[bfbs], pub_b, sub_b, count) for _ in range(args.repeats))
            row += "|" + f"{avg[bfbs]:>9.1f}{mps:>12,.0f}{mps*avg[bfbs]/1e6:>9.1f}"
        print(row)

if __name__ == "__main__":
    main()

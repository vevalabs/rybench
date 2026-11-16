#!/usr/bin/env python3
"""Publish over a transport, receive, and dump the received messages as JSON.

Runs a publisher (random or fixed replay) and a subscriber that deserializes each
received FlatBuffer back to JSON (one object per line). Lets you eyeball that the
transport delivers correct, complete messages, and vary the seed.

    benchmark/perf/verify_json.py                       # random, seed 1, iceoryx2
    benchmark/perf/verify_json.py --gen fixed --seed 7
    benchmark/perf/verify_json.py --transport zenoh --schema schemas/osi_vehiclemotion.bfbs

Output file is printed at the end (default: benchmark/perf/received.json).
"""
import argparse, json, os, signal, subprocess, tempfile, time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BIN  = os.path.join(REPO, "build", "bin")

TR = {  # transport -> (app, publish block, consume-block template with {out})
 "iceoryx2": ("rybench-iceoryx2",
   '[iceoryx2]\nmode="publish"\nservice="VJ"\ninstance="D"\nevent="E"\nmax_payload_size=32768\nrate_hz=10000000\n',
   '[iceoryx2]\nmode="consume"\nservice="VJ"\ninstance="D"\nevent="E"\nmax_payload_size=32768\nrate_hz=10000000\noutput="{out}"\noutput_json="true"\n'),
 "zenoh": ("rybench-zenoh",
   '[zenoh]\nmode="publish"\nkey_expr="VJ/D/E"\nmax_payload_size=32768\nrate_hz=10000000\n',
   '[zenoh]\nmode="consume"\nkey_expr="VJ/D/E"\nmax_payload_size=32768\nrate_hz=10000000\noutput="{out}"\noutput_json="true"\n'),
}

def cfgdir(bfbs, gen, transport):
    d = tempfile.mkdtemp(); os.makedirs(d + "/n")
    open(d + "/n/config.toml", "w").write(
        f'[runtime]\napp_name="n"\n[schema]\nbfbs="{bfbs}"\n{gen}[serializer]\ntype="flatbuffers"\n{transport}')
    return d

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gen", choices=["random", "fixed"], default="random")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--transport", choices=list(TR), default="iceoryx2")
    ap.add_argument("--schema", default="schemas/osi_hostvehicledata.bfbs")
    ap.add_argument("--window", type=float, default=1.5)
    ap.add_argument("--out", default=os.path.join(os.path.dirname(__file__), "received.json"))
    args = ap.parse_args()
    app, pub_b, sub_b = TR[args.transport]

    if args.gen == "random":
        pub_gen = f'[generator]\nalgorithm="schema_guided"\nseed={args.seed}\ncount=0\n'
    else:
        dump = "/tmp/verify_json.dump"
        subprocess.run([os.path.join(BIN, "rybench-filewriter"), "n"], cwd=REPO,
            env=dict(os.environ, RYBENCH_CONFIG=cfgdir(args.schema,
                f'[generator]\nalgorithm="schema_guided"\nseed={args.seed}\ncount=1000\n',
                f'[filewriter]\noutput="{dump}"\nrate_hz=10000000\n')),
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        pub_gen = f'[generator]\nalgorithm="fixed"\nseed={args.seed}\ncount=0\nreplay="{dump}"\n'

    subd = cfgdir(args.schema, '[generator]\nalgorithm="fixed"\nseed=1\ncount=0\nreplay="/dev/null"\n',
                  sub_b.format(out=args.out))
    pubd = cfgdir(args.schema, pub_gen, pub_b)

    sub = subprocess.Popen([os.path.join(BIN, app), "n"], cwd=REPO,
        env=dict(os.environ, RYBENCH_CONFIG=subd), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.5)
    pub = subprocess.Popen([os.path.join(BIN, app), "n"], cwd=REPO,
        env=dict(os.environ, RYBENCH_CONFIG=pubd), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(args.window)
    sub.send_signal(signal.SIGINT); sub.wait(timeout=10)
    pub.send_signal(signal.SIGINT)
    try: pub.wait(timeout=8)
    except subprocess.TimeoutExpired: pub.kill()

    lines = [l for l in open(args.out).read().splitlines() if l.strip()] if os.path.exists(args.out) else []
    print(f"\n{args.transport}  {args.gen}  seed={args.seed}  ->  {args.out}")
    print(f"received {len(lines)} JSON messages")
    if lines:
        print("first message:")
        print(json.dumps(json.loads(lines[0]), indent=2)[:600], "...")

if __name__ == "__main__":
    main()

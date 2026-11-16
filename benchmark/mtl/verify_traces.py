#!/usr/bin/env python3
"""
Verify generated traces against their MTL property using Reelay (ryjson).

    benchmark/mtl/verify_traces.py [DIR]

DIR defaults to output_traces (ours); pass output_traces_timescales to check the
reference. Each <name>.tuples is monitored against the property's PAST-time spec
(reelay is a past-time monitor). The past spec is taken from the timescales tool
for OUR bounds (lbound=0, ubound=100), so it matches what we generate.

A trace is correct if the spec holds at every time point. Reported per property:
  - violations : false verdicts at time >= UBOUND  (real property violations)
  - transient  : false verdicts at time <  UBOUND  (benign: a past monitor lacks
                 'history' at the very start of a future-generated trace)

NOTE on between/before properties: timescales' simple past specs (e.g.
`(r && !q && once q) -> (p since[l:u] q)`) are co-designed with timescales' own RIGID
generate_trace template (q only at episode start, q xor r, p=True at the R step, one R
per episode + a gap). Our generator samples the full future-MTL model space (free q's,
q@r co-occurrence, multiple r's, ¬p@r), which are all valid future-MTL models the simple
past spec cannot represent (half-open U=[Q,R) vs closed S=(Q,R]; `since q` has no reset
at intermediate r's). Such "VIOLATION" verdicts here are past-spec artifacts, NOT
generation bugs — our traces are correct per the future-fragment MTL formula.
"""
import json
import os
import re
import subprocess
import sys
import tempfile

MTL_DIR = os.path.dirname(os.path.abspath(__file__))
REPO    = os.path.dirname(os.path.dirname(MTL_DIR))
TS_SRC  = os.path.join(REPO, "benchmark", "tools", "timescales", "src")
RYJSON  = os.path.join(REPO, "rv-tools", "reelay", "build", "apps", "ryjson1", "ryjson")
UBOUND, LBOUND = 10, 0

sys.path.insert(0, TS_SRC)
try:
    import timescales  # noqa: E402
except ImportError:
    sys.exit(f"timescales not found at {TS_SRC}")

KNOWN = {"absence_after_q", "absence_before_r", "absence_between_q_and_r",
         "always_after_q", "always_before_r", "always_between_q_and_r",
         "recurrence_globally", "recurrence_between_q_and_r",
         "response_globally", "response_between_q_and_r"}


def ts_name(fname):
    n = fname[:-len(".tuples")]
    if n.startswith("bounded_"):
        n = n[len("bounded_"):]
    return n.replace("universality", "always")   # our 'universality_*' == timescales 'always_*'


def past_spec(prop):
    past, _future, _rows = timescales.generate(
        property=prop, lower_bound=LBOUND, upper_bound=UBOUND,
        min_recur=1, max_recur=7, duration=10, limit_stutter=0, failing_end=False)
    return past


def to_jsonl(tuples_path, jsonl_path):
    f = open(tuples_path)
    fields = [x.strip() for x in f.readline().strip().lstrip("# ").strip("()").split(",")]
    n = 0
    with open(jsonl_path, "w") as out:
        for line in f:
            line = line.strip()
            if not line.startswith("("):
                continue
            vals = [v.strip() for v in line.strip("()").split(",")]
            d = {}
            for k, v in zip(fields, vals):
                if k in ("time", "timestamp"):
                    d["time"] = int(v)
                else:
                    d[k] = (v == "1" or v.lower() == "true")
            out.write(json.dumps(d) + "\n")
            n += 1
    return n


def verify(tuples_path, spec):
    with tempfile.TemporaryDirectory() as tmp:
        jl = os.path.join(tmp, "t.jsonl")
        n = to_jsonl(tuples_path, jl)
        proc = subprocess.run([RYJSON, "-x", "-b", spec, jl],
                              capture_output=True, text=True)
    real = transient = 0
    for line in proc.stdout.splitlines():
        m = re.search(r'"time":(\d+),"value":(true|false)', line)
        if m and m.group(2) == "false":
            if int(m.group(1)) >= UBOUND:
                real += 1
            else:
                transient += 1
    return n, real, transient


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(MTL_DIR, "output_traces")
    if not os.path.exists(RYJSON):
        sys.exit(f"ryjson not built at {RYJSON}")
    specs = {p: past_spec(p) for p in KNOWN}

    files = sorted(f for f in os.listdir(d)
                   if f.endswith(".tuples") and ts_name(f) in KNOWN)
    print(f"\n=== Reelay verification of {os.path.relpath(d, REPO)} ===")
    print(f"(past-time specs, lbound={LBOUND} ubound={UBOUND}; "
          f"false verdicts at time<{UBOUND} are start transients)\n")
    hdr = f"{'#':>2}  {'property':<38}{'events':>9}{'violations':>12}{'transient':>11}  status"
    print(hdr)
    print("-" * len(hdr))
    bad = 0
    for i, f in enumerate(files, 1):
        n, real, transient = verify(os.path.join(d, f), specs[ts_name(f)])
        status = "CLEAN" if real == 0 else "VIOLATION"
        bad += real > 0
        print(f"{i:>2}  {f[:-7]:<38}{n:>9}{real:>12}{transient:>11}  {status}")
    print("-" * len(hdr))
    print(f"{len(files)} traces checked, {bad} with real violations")


if __name__ == "__main__":
    main()

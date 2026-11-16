#!/usr/bin/env python3
"""
Validate our future-MTL traces with a PAST monitor via the reversal duality.

    benchmark/mtl/verify_reverse.py [DIR] [property-substring]

Instead of timescales' hand-written past spec (which is co-designed with a rigid
trace template and flags benign artifacts on the between/before properties — see
verify_traces.py), this uses the EXACT temporal mirror of the FUTURE formula and
runs it on the time-REVERSED trace:

    (sigma, i) |= phi        <=>        (reverse(sigma), n-1-i) |= mirror(phi)

mirror(): always->historically, eventually->once, until->since; booleans, atoms
and interval bounds unchanged. reelay is a past monitor, so the mirrored spec is
exactly what it can check, with no template mismatch.

If our generator is correct per future-MTL, the reversed+mirrored run is CLEAN
(no false verdicts at time >= UBOUND) for EVERY property, including the ones the
timescales past spec reports as "VIOLATION". A false at time < UBOUND is the
mirror of the future end-of-trace lookahead transient (now at the reversed start).
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
    return n.replace("universality", "always")


def future_spec(prop):
    """The FUTURE formula the generator actually targets — read straight from the
    property's schema.fbs `mtl:` attribute (NOT from timescales.generate, whose
    between-scope guard drops the [0,10] bound and thus flags spurious violations)."""
    stem = "bounded_" + prop.replace("always", "universality")
    fbs = os.path.join(MTL_DIR, stem, "schema.fbs")
    m = re.search(r'mtl:\s*"([^"]*)"', open(fbs).read())
    if not m:
        raise RuntimeError(f"no mtl: formula in {fbs}")
    return m.group(1)


def mirror(future):
    """Temporal mirror of a future formula into reelay past syntax, bounds preserved.
    Handles the schema syntax: always[0,inf]/eventually[a,b]/until[a,b], implies, and,
    not, bare atoms."""
    s = future
    s = s.replace("[0,inf]", "")                       # unbounded always -> historically
    s = re.sub(r'\[\s*(\d+)\s*,\s*(\d+)\s*\]', r'[\1:\2]', s)   # [a,b] -> [a:b]
    # temporal operators future -> past (whole word; any [a:b] bound stays attached)
    s = re.sub(r'\balways\b',     'historically', s)
    s = re.sub(r'\beventually\b', 'once',         s)
    s = re.sub(r'\buntil\b',      'since',        s)
    s = re.sub(r'\bimplies\b',    '->',           s)
    # tolerate timescales-style boolean tokens too (harmless if absent)
    s = s.replace('&&', ' and ').replace('||', ' or ')
    s = re.sub(r'!\s*(?=[{(])', 'not ', s)
    # brace bare atoms p,q,r,s (e.g. "q implies ...")
    s = re.sub(r'(?<![\w{])([pqrs])(?![\w}])', r'{\1}', s)
    s = re.sub(r'\s+', ' ', s).strip()
    return s


def reverse_to_jsonl(tuples_path, jsonl_path):
    """Reverse the trace in time and re-index timestamps 0..n-1 (discrete)."""
    f = open(tuples_path)
    fields = [x.strip() for x in f.readline().strip().lstrip("# ").strip("()").split(",")]
    rows = []
    for line in f:
        line = line.strip()
        if not line.startswith("("):
            continue
        vals = [v.strip() for v in line.strip("()").split(",")]
        d = {}
        for k, v in zip(fields, vals):
            if k in ("time", "timestamp"):
                continue
            d[k] = (v == "1" or v.lower() == "true")
        rows.append(d)
    rows.reverse()
    with open(jsonl_path, "w") as out:
        for t, d in enumerate(rows):
            d["time"] = t
            out.write(json.dumps(d) + "\n")
    return len(rows)


def verify(tuples_path, spec):
    with tempfile.TemporaryDirectory() as tmp:
        jl = os.path.join(tmp, "t.jsonl")
        n = reverse_to_jsonl(tuples_path, jl)
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
    return n, real, transient, proc.stderr.strip()


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else os.path.join(MTL_DIR, "output_traces")
    filt = sys.argv[2] if len(sys.argv) > 2 else ""
    if not os.path.exists(RYJSON):
        sys.exit(f"ryjson not built at {RYJSON}")

    specs = {p: mirror(future_spec(p)) for p in KNOWN}
    if filt:
        for p in sorted(specs):
            if filt in p:
                print(f"{p:32} mirror: {specs[p]}")
        print()

    files = sorted(f for f in os.listdir(d)
                   if f.endswith(".tuples") and ts_name(f) in KNOWN
                   and (not filt or filt in ts_name(f)))
    print(f"=== reversed-trace + mirror-past verification of {os.path.relpath(d, REPO)} ===")
    print(f"(mirror of the FUTURE spec, lbound={LBOUND} ubound={UBOUND}; "
          f"false at time<{UBOUND} = mirrored end transient)\n")
    hdr = f"{'#':>2}  {'property':<38}{'events':>9}{'violations':>12}{'transient':>11}  status"
    print(hdr); print("-" * len(hdr))
    bad = 0
    for i, f in enumerate(files, 1):
        n, real, transient, err = verify(os.path.join(d, f), specs[ts_name(f)])
        status = "CLEAN" if real == 0 else "VIOLATION"
        if err:
            status = "ERR: " + err[:40]
        bad += real > 0
        print(f"{i:>2}  {f[:-7]:<38}{n:>9}{real:>12}{transient:>11}  {status}")
    print("-" * len(hdr))
    print(f"{len(files)} traces checked, {bad} with real violations")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Independently verify the generated traces against their FUTURE-time MTL
formula using MonPoly (bounded-future MFOTL).

For each property we monitor the NEGATION of its per-step obligation — a
violation formula, `antecedent AND NOT consequent`, in MonPoly syntax. MonPoly
emits one line per timepoint at which the property is violated; empty output
means the trace satisfies the future formula everywhere MonPoly can decide it.
This checks whether the Reelay past-spec violations on the `between` scopes are
past-spec artifacts (clean here) or real generation errors.

    benchmark/mtl/verify_monpoly.py                       # uses output_traces/*.tuples
    benchmark/mtl/verify_monpoly.py --monpoly /path/to/monpoly

Requires the generated traces (run gen_traces.py first) and a monpoly binary.
"""
import argparse
import os
import subprocess
import sys
import tempfile

MTL_DIR = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(MTL_DIR, "output_traces")

SIG = "p()\nq()\nr()\ns()\n"

# Per-property violation formula (antecedent AND NOT consequent) of the future
# obligation, in MonPoly bounded-future MFOTL syntax. Any timepoint MonPoly
# reports for these is a genuine future-MTL violation of that property.
VIOL = {
    "bounded_absence_after_q":
        "q() AND (EVENTUALLY[0,10] p())",
    "bounded_absence_before_r":
        "(EVENTUALLY[0,10] r()) AND "
        "(NOT ((NOT p()) UNTIL[0,10] r()))",
    "bounded_absence_between_q_and_r":
        "(q() AND (NOT r()) AND (EVENTUALLY[0,10] r())) AND "
        "(NOT ((NOT p()) UNTIL[0,10] r()))",
    "bounded_recurrence_between_q_and_r":
        "(q() AND (NOT r()) AND (EVENTUALLY[0,10] r())) AND "
        "(NOT ((EVENTUALLY[0,10] (p() OR r())) UNTIL[0,10] r()))",
    "bounded_recurrence_globally":
        "NOT (EVENTUALLY[0,10] p())",
    "bounded_response_between_q_and_r":
        "(q() AND (NOT r()) AND (EVENTUALLY[0,10] r())) AND p() AND "
        "(NOT (((NOT r()) UNTIL[0,10] s()) UNTIL[0,10] r()))",
    "bounded_response_globally":
        "p() AND (NOT (EVENTUALLY[0,10] s()))",
    "bounded_universality_after_q":
        "q() AND (EVENTUALLY[0,10] (NOT p()))",
    "bounded_universality_before_r":
        "(EVENTUALLY[0,10] r()) AND (NOT (p() UNTIL[0,10] r()))",
    "bounded_universality_between_q_and_r":
        "(q() AND (NOT r()) AND (EVENTUALLY[0,10] r())) AND "
        "(NOT (p() UNTIL[0,10] r()))",
}


def tuples_to_log(tuples_path, log_path):
    """Convert a .tuples file to a MonPoly log: '@t <true atoms>' per line."""
    lines = open(tuples_path).read().splitlines()
    if not lines:
        return 0
    cols = [c.strip() for c in lines[0].strip("# ()").split(",")]  # timestamp, p, q, ...
    atoms = [c for c in cols if c != "timestamp"]
    n = 0
    with open(log_path, "w") as out:
        for ln in lines[1:]:
            ln = ln.strip()
            if not ln.startswith("("):
                continue
            row = dict(zip(cols, (v.strip() for v in ln.strip("()").split(","))))
            true_atoms = " ".join(f"{a}()" for a in atoms if row.get(a) == "1")
            out.write(f"@{row['timestamp']} {true_atoms}\n")
            n += 1
    return n


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--monpoly", default="monpoly", help="path to the monpoly binary")
    ap.add_argument("--properties", nargs="+", default=None)
    ap.add_argument("--show", action="store_true", help="print each violation line")
    args = ap.parse_args()

    props = args.properties or sorted(VIOL)
    print(f"{'property':<40}{'events':>8}{'violations':>12}  status")
    print("-" * 72)
    for name in props:
        tp = os.path.join(OUT_DIR, name + ".tuples")
        if not os.path.exists(tp):
            print(f"{name:<40}{'-':>8}{'-':>12}  no trace (run gen_traces.py)")
            continue
        with tempfile.TemporaryDirectory() as tmp:
            sig = os.path.join(tmp, "s.sig"); open(sig, "w").write(SIG)
            fml = os.path.join(tmp, "f.mfotl"); open(fml, "w").write(VIOL[name] + "\n")
            log = os.path.join(tmp, "t.log"); n = tuples_to_log(tp, log)
            try:
                p = subprocess.run(
                    [args.monpoly, "-sig", sig, "-formula", fml, "-log", log],
                    capture_output=True, text=True, timeout=1200)
            except FileNotFoundError:
                sys.exit(f"monpoly not found at '{args.monpoly}' — build it or pass --monpoly")
            viol = [l for l in p.stdout.splitlines() if l.strip().startswith("@")]
            status = "CLEAN" if not viol else "VIOLATION"
            print(f"{name:<40}{n:>8}{len(viol):>12}  {status}")
            if args.show:
                for l in viol[:20]:
                    print("     " + l)


if __name__ == "__main__":
    main()

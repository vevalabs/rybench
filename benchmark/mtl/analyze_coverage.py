#!/usr/bin/env python3
"""Per-formula node coverage over committed steps.

    benchmark/mtl/analyze_coverage.py <N>
    benchmark/mtl/analyze_coverage.py 100000 --seed 1

For each MTL case an N-step trace is generated with coverage instrumentation on
(RYBENCH_COVERAGE). For every node of the *converted* formula tree we count, over
the committed steps only:

  reached   - how many times the generator's traversal reached this node
  fired     - how many times a temporal node discharged (F/U/G/R fired its child)
  fire-rate - fired / reached (depth-independent firing propensity)

A node reached far less than its siblings is *starved*: the generator rarely
commits to the branch that leads to it (a forced-branch coverage hole). Each
formula prints its numbered tree followed by the coverage table, so node ids map
to the figure. A summary flags starved nodes per formula.
"""
import argparse
import math
import os
import re
import subprocess
import sys
import tempfile

MTL_DIR = os.path.dirname(os.path.abspath(__file__))
REPO    = os.path.dirname(os.path.dirname(MTL_DIR))
BINARY  = os.path.join(REPO, "build", "bin", "rybench-filewriter")

EXCLUDE = {"eventual_p", "recurrence_demorgan"}

COV_RE = re.compile(
    r"\[(\d+)\]\s+(.+?)\s+reached=(\d+)\s+fired=(\d+)\s+fire-rate=([\d.]+)"
    r"(?:\s+choice=L:(\d+)\s+R:(\d+))?")

VAR_RE = re.compile(
    r"^\s+(\S+)\s+forced-true=(\d+)\s+forced-false=(\d+)\s+free=(\d+)\s+"
    r"\(free-true=(\d+)\s+free-false=(\d+)\)\s+\(([\d.]+)% determined\)", re.M)


def properties():
    return sorted(d for d in os.listdir(MTL_DIR)
                  if os.path.isfile(os.path.join(MTL_DIR, d, "config.toml"))
                  and d not in EXCLUDE)


def override(cfg, count):
    cfg = re.sub(r'(?m)^\s*count\s*=.*$',  f'count = {count}',      cfg)
    cfg = re.sub(r'(?m)^\s*output\s*=.*$', 'output = "/dev/null"',  cfg)
    return cfg


def run_case(name, count, seed):
    cfg = open(os.path.join(MTL_DIR, name, "config.toml")).read()
    with tempfile.TemporaryDirectory() as tmp:
        node = os.path.join(tmp, name)
        os.makedirs(node)
        open(os.path.join(node, "config.toml"), "w").write(override(cfg, count))
        env = dict(os.environ, RYBENCH_CONFIG=tmp,
                   RYBENCH_COVERAGE="1", RYBENCH_DUMP_TREE="1")
        if seed is not None:
            env["RYBENCH_SEED"] = str(seed)
        p = subprocess.run([BINARY, name], cwd=REPO, env=env,
                           capture_output=True, text=True)
    return p.stdout + p.stderr


def parse_tree(text):
    lines = text.splitlines()
    start = next((i for i, l in enumerate(lines)
                  if "converted formula tree" in l), None)
    if start is None:
        return []
    out = []
    for l in lines[start + 1:]:
        if l.startswith("Generator:"):
            break
        out.append(l)
    return out


def parse_cov(text):
    out = []
    for m in COV_RE.finditer(text):
        d = {"id": int(m.group(1)), "label": m.group(2).strip(),
             "reached": int(m.group(3)), "fired": int(m.group(4)),
             "rate": float(m.group(5))}
        if m.group(6) is not None:
            d["chL"], d["chR"] = int(m.group(6)), int(m.group(7))
        out.append(d)
    return out


def parse_vars(text):
    return [{"var": m.group(1), "ft": int(m.group(2)), "ff": int(m.group(3)),
             "free": int(m.group(4)), "freeT": int(m.group(5)),
             "freeF": int(m.group(6)), "pdet": float(m.group(7))}
            for m in VAR_RE.finditer(text)]


def zscore(k, n, p):
    """Signed z-score of count k against a Binomial(n, p) expectation:
    how many standard deviations the observed count is from n*p."""
    if n == 0:
        return 0.0
    std = math.sqrt(n * p * (1.0 - p))
    return (k - n * p) / std if std > 0 else 0.0


def node_analysis(r, zthr=5.0):
    """Return (z_str, tag) for the coverage table.
      F/U  -> single z vs Binom(reached, 1/6); pos-/neg-biased (over/under-fires).
      xor  -> single z on the left branch vs Binom(n, 1/2); left-/right-biased
              (partition: exactly one branch per step, so they compete).
      or   -> TWO independent z's (each branch vs Binom(reached, 0.625)); each branch
              flagged pos/neg on its own, since both can be taken in the same step."""
    lab = r["label"]
    m = re.match(r'([FUGR])\[(\d+),(\d+|inf)\]', lab)
    if m and m.group(1) in ('F', 'U') and m.group(3) != 'inf':
        z = zscore(r["fired"], r["reached"], 1.0 / 6.0)
        tag = "pos-biased" if z > zthr else "neg-biased" if z < -zthr else "unbiased"
        return f"{z:+.1f}", tag
    if lab == "xor" and "chL" in r:
        z = zscore(r["chL"], r["chL"] + r["chR"], 0.5)
        tag = "unbiased" if abs(z) <= zthr else ("left-biased" if z > 0 else "right-biased")
        return f"{z:+.1f}", tag
    if lab == "or" and "chL" in r:
        n = r["reached"]
        both = r["chL"] + r["chR"] - n  # steps where BOTH branches were taken
        # If both-branches is rare, the branches are effectively exclusive (taking
        # both conflicts), so score the split against a fair coin like an xor.
        if n > 0 and both < 0.05 * n:
            z = zscore(r["chL"], r["chL"] + r["chR"], 0.5)
            tag = "unbiased" if abs(z) <= zthr else ("left-biased" if z > 0 else "right-biased")
            return f"{z:+.1f}", tag
        # Genuine inclusive or: independent branches, each against p = 0.625.
        zl = zscore(r["chL"], n, 0.625)
        zr = zscore(r["chR"], n, 0.625)
        d = lambda z: "pos" if z > zthr else "neg" if z < -zthr else "unb"
        return f"{zl:+.0f},{zr:+.0f}", f"L:{d(zl)},R:{d(zr)}"
    return "", ""


def is_biased(tag):
    """Whether a node's tag flags any bias (for the summary count)."""
    if tag.endswith("-biased"):
        return True
    if tag.startswith("L:"):
        return "pos" in tag or "neg" in tag
    return False


def main():
    ap = argparse.ArgumentParser(
        description="Per-formula node coverage over committed steps.")
    ap.add_argument("N", type=int, help="trace length (committed steps per case)")
    ap.add_argument("--seed", type=int, default=None,
                    help="RYBENCH_SEED override (default: each config's own seed)")
    args = ap.parse_args()

    if not os.path.exists(BINARY):
        sys.exit(f"missing {BINARY} — build rybench-filewriter first")

    props = properties()
    for name in props:                       # (re)compile schemas the configs reference
        fbs = os.path.join(MTL_DIR, name, "schema.fbs")
        if os.path.exists(fbs):
            subprocess.run(["flatc", "--binary", "--schema", "-o",
                            os.path.join(MTL_DIR, name), fbs], check=True)

    summary = []
    for name in props:
        out  = run_case(name, args.N, args.seed)
        tree = parse_tree(out)
        cov  = parse_cov(out)

        print(f"\n{'='*72}\n{name}   (N={args.N})\n{'='*72}")
        for l in tree:
            print(l)

        if not cov:
            print("  (no coverage — the generator produced no committed steps)")
            summary.append((name, 0, 0))
            continue

        print(f"\n  {'id':>3} {'node':<14}{'reached':>10}{'fired':>10}"
              f"{'fire-rate':>10}{'z':>14}  {'tag':<16}")
        print("  " + "-" * 74)
        for r in cov:
            zs, tag = node_analysis(r)
            print(f"  {r['id']:>3} {r['label']:<14}{r['reached']:>10}"
                  f"{r['fired']:>10}{r['rate']:>10.3f}{zs:>14}  {tag:<16}")

        vars_ = parse_vars(out)
        if vars_:
            print(f"\n  {'var':<6}{'forced-T':>10}{'forced-F':>10}{'free':>10}"
                  f"{'free-T':>9}{'free-F':>9}{'%det':>8}")
            print("  " + "-" * 62)
            for v in vars_:
                print(f"  {v['var']:<6}{v['ft']:>10}{v['ff']:>10}{v['free']:>10}"
                      f"{v['freeT']:>9}{v['freeF']:>9}{v['pdet']:>8.2f}")
        biased = sum(1 for r in cov if is_biased(node_analysis(r)[1]))
        summary.append((name, biased, len(cov)))

    print(f"\n{'='*72}\nsummary  (biased nodes by z-score, |z| > 5)\n{'='*72}")
    print(f"  {'property':<42}{'biased':>8}{'nodes':>7}")
    for name, b, n in summary:
        print(f"  {name:<42}{b:>8}{n:>7}")


if __name__ == "__main__":
    main()

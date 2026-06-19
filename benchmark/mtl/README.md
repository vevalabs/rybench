# MTL Benchmark Properties (future fragment)

The ten timed properties from the timescales benchmark (Ulus), encoded in the
**future fragment** of MTL for RyBench's `property_guided` generator. Bounds:
**u = 100, l = 0**.

Each subdirectory has:
- `schema.fbs` — an `Event` table with `timestamp:ulong`, the boolean atoms it
  uses, and the formula in the `mtl` table attribute.
- `config.toml` — a `property_guided` node config writing to `output_traces/`.

Operator mapping to our grammar: `□ → always[l,u]`, `◇ → eventually[l,u]`,
`U → until` (unbounded) / `until[l,u]` (bounded), `¬ → not`, `∧ → and`,
`∨ → or`, `→ → implies`. The outer "globally" is `always[0,inf]`.

## Formulas

| Property | Atoms | Future formula |
|----------|-------|----------------|
| absence_after_q | p q | `always[0,inf] (q implies always[0,100] (not p))` |
| absence_before_r | p r | `always[0,inf] (eventually[0,100] r implies ((not p) until r))` |
| absence_between_q_and_r | p q r | `always[0,inf] ((q and ((not r) and eventually[0,inf] r)) implies ((not p) until[0,100] r))` |
| universality_after_q | p q | `always[0,inf] (q implies always[0,100] p)` |
| universality_before_r | p r | `always[0,inf] (eventually[0,100] r implies (p until r))` |
| universality_between_q_and_r | p q r | `always[0,inf] ((q and ((not r) and eventually[0,inf] r)) implies (p until[0,100] r))` |
| recurrence_globally | p | `always[0,inf] (eventually[0,100] p)` |
| recurrence_between_q_and_r | p q r | `always[0,inf] ((q and ((not r) and eventually[0,inf] r)) implies ((eventually[0,100] (p or r)) until r))` |
| response_globally | p s | `always[0,inf] (p implies eventually[0,100] s)` |
| response_between_q_and_r | p q r s | `always[0,inf] ((q and ((not r) and eventually[0,inf] r)) implies ((p implies ((not r) until[0,100] ((not r) and s))) until r))` |

## Parser support

`parseMTLNode` handles everything these formulas use (all 10 parse):

1. **`inf` bound** — unbounded `always`/`eventually`/`until` via the keyword
   `inf` (e.g. `always[0,inf]`, `eventually[0,inf]`). A bound is a number or `inf`.
2. **`until[l,u]`** — bounded until (e.g. `until[0,100]`); bare `until` stays
   unbounded.

Grammar style is exact-parenthesis, no precedence: every binary op is written
`( phi op phi )`.

## Build & run

1. Build the filewriter app:
   ```bash
   scripts/build.sh rybench-filewriter        # or: cmake --build build_release --target rybench-filewriter
   ```

2. Compile the schemas to `.bfbs` (what each `config.toml` references):
   ```bash
   for d in benchmark/mtl/*/; do flatc --binary --schema -o "$d" "$d/schema.fbs"; done
   ```

3. Run a property — generates a trace into `output_traces/`:
   ```bash
   export RYBENCH_CONFIG=benchmark/mtl
   ./build_release/bin/rybench-filewriter bounded_absence_after_q
   ```
   Each config sets `count = 1000` (stops after 1000 messages), `rate_hz = 0`
   (unthrottled), serializer `json`, output `output_traces/<name>.out`.

   Output file framing: each message is `[uint32 little-endian size][json bytes]`.

> ⚠️ **AST only, no trace yet.** `property_guided` is wired into the build and
> factory: running a config loads the schema and prints the parsed +
> preprocessed AST. But `PropertyGuidedMTLGenerator::generate()` is still a stub
> that returns an empty message, so **no trace is written yet** — after the AST
> the engine loops "Generated empty message, skipping". Implementing
> `generate()` is what produces formula-consistent traces.

## Print the AST per property

Each one-liner loads the schema and prints the parsed + preprocessed tree, then
stops at the `Started` line (no "skipping" flood, no Ctrl+C needed):

```bash
RYBENCH_CONFIG=benchmark/mtl ./build_release/bin/rybench-filewriter bounded_absence_after_q 2>&1 | sed '/^Started/q'
RYBENCH_CONFIG=benchmark/mtl ./build_release/bin/rybench-filewriter bounded_absence_before_r 2>&1 | sed '/^Started/q'
RYBENCH_CONFIG=benchmark/mtl ./build_release/bin/rybench-filewriter bounded_absence_between_q_and_r 2>&1 | sed '/^Started/q'
RYBENCH_CONFIG=benchmark/mtl ./build_release/bin/rybench-filewriter bounded_universality_after_q 2>&1 | sed '/^Started/q'
RYBENCH_CONFIG=benchmark/mtl ./build_release/bin/rybench-filewriter bounded_universality_before_r 2>&1 | sed '/^Started/q'
RYBENCH_CONFIG=benchmark/mtl ./build_release/bin/rybench-filewriter bounded_universality_between_q_and_r 2>&1 | sed '/^Started/q'
RYBENCH_CONFIG=benchmark/mtl ./build_release/bin/rybench-filewriter bounded_recurrence_globally 2>&1 | sed '/^Started/q'
RYBENCH_CONFIG=benchmark/mtl ./build_release/bin/rybench-filewriter bounded_recurrence_between_q_and_r 2>&1 | sed '/^Started/q'
RYBENCH_CONFIG=benchmark/mtl ./build_release/bin/rybench-filewriter bounded_response_globally 2>&1 | sed '/^Started/q'
RYBENCH_CONFIG=benchmark/mtl ./build_release/bin/rybench-filewriter bounded_response_between_q_and_r 2>&1 | sed '/^Started/q'
```

# Benchmark

Performance benchmarks for RyBench, with a reference comparison against
[`timescales`](https://github.com/doganulus/timescales) — a Metric Temporal
Logic (MTL) trace/benchmark generator. The long-term goal is to compare
RyBench's `property_guided` MTL generator against `timescales` on the same
properties.

## Layout

```
benchmark/
├── tools/        # cloned reference tools (gitignored — not committed)
│   └── timescales/
├── scripts/      # benchmark harnesses
│   └── bench_timescales.py
├── results/      # stored baselines for backward / regression checking
│   └── timescales_baseline.json
└── README.md
```

## Setup

The `tools/` dir is **gitignored**; clone the reference locally once:

```bash
git clone --depth 1 https://github.com/doganulus/timescales.git \
    benchmark/tools/timescales
```

`timescales` needs only Python 3.8+ (its core trace generation has no
third-party deps).

## Run

```bash
python3 benchmark/scripts/bench_timescales.py
# custom sweep:
python3 benchmark/scripts/bench_timescales.py --durations 1000 1000000 --repeats 7
```

It times `timescales.generate()` per property × duration (best-of N, fixed RNG
seed) and writes `results/timescales_baseline.json`.

## Current baseline — timescales (reference)

Python 3.13, best-of-5, `ubound=100`. Throughput is **rows (events) per second**
for in-memory trace generation (Python tuples; **no wire serialization**).

| property                | 1k        | 100k      | 1M        |
|-------------------------|-----------|-----------|-----------|
| absence_after_q         | ~1.57 M/s | ~1.46 M/s | ~1.11 M/s |
| absence_between_q_and_r | ~1.82 M/s | ~1.83 M/s | ~1.18 M/s |
| recurrence_globally     | ~2.03 M/s | ~2.18 M/s | ~1.36 M/s |
| response_globally       | ~1.95 M/s | ~1.88 M/s | ~1.23 M/s |

So timescales generates **~1.1–2.2 M boolean-valuation rows/sec**, dropping a bit
at 1M rows (Python list/GC pressure).

> ⚠️ Not yet comparable to RyBench numbers: timescales emits plain Python
> `(time, q, p, r)` tuples with **no serialization**, whereas RyBench's
> `schema_guided` (~358 k msg/s) encodes full FlatBuffer messages of a complex
> schema. A fair head-to-head needs the RyBench **`property_guided`** MTL
> generator (same MTL→trace task) — and its `generate()` currently returns an
> empty `Message` (the FlatBuffer encoding of the valuation is a `// TODO`).

## Next steps

1. Implement `PropertyGuidedMTLGenerator::generate()` FlatBuffer encoding
   (valuation + timestamp) so it emits real messages.
2. Add `bench_rybench.py` (or extend the existing `scripts/benchmark.sh`) to
   measure the `property_guided` path on the same properties/durations.
3. Store both sides in `results/` and add a comparison/regression check.

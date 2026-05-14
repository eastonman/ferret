# `nested_call_depth` — how to run

Probes the depth of the CPU's **Return Address Stack (RAS)** by emitting a
chain of N nested `call`/`ret` pairs. Per-call cost is flat while
N ≤ RAS capacity; once N exceeds it, the oldest return addresses are
evicted, those rets miss RAS, and per-call cost steps up to a higher
plateau. The cliff position is the RAS capacity.

The construction defeats BTB-indirect prediction on `ret` so the cliff is
not masked by a fallback predictor — see
[the kernel-pattern note](../superpowers/specs/2026-05-12-ras-depth-kernel-pattern.md)
and [the full design](../superpowers/specs/2026-05-12-nested-call-depth-design.md)
for the construction details.

## The two-step workflow

ferret reports per-call cost in **CPU cycles** when you supply the running
core frequency, and in nanoseconds otherwise. Frequency probing is a
separate step against the same core; see the project README for the
general protocol.

```sh
# Step 1: probe the running frequency on the core you'll measure on.
build/ferret run dependent_chain_throughput --core=3 --out=/tmp/freq.csv
python3 scripts/freq.py /tmp/freq.csv
# → estimated_freq=4.490GHz

# Step 2: sweep depth on the same core, with --freq.
build/ferret run nested_call_depth --core=3 \
    --depth=1..64 --freq=4.490GHz \
    --reps=7 --warmup=2 \
    --out=/tmp/ras.csv

# Step 3: plot — picks cycles_per_site automatically because freq was set.
python3 scripts/plot.py /tmp/ras.csv --out=/tmp/ras.png
```

## CLI surface

| flag                  | meaning                                                  |
| --------------------- | -------------------------------------------------------- |
| `--depth=A..B`        | Sweep nesting depth from A to B inclusive, step 1.       |
| `--depth=v1,v2,…`     | Sweep an explicit list of depths.                        |
| `--path_table_rows=N` | Per-iteration dispatch table row count. Default 4096. Must be a power of two ≥ 2. Bigger ⇒ longer period before the dispatch pattern repeats; also bigger memory footprint (`N × (depth+1)` bytes). |
| `--freq=…`            | Standard ferret flag. Enables `cycles_per_site_*` columns. |
| `--core=…`            | Standard ferret pinning flag. Pin probe and benchmark to the same core. |
| `--reps=K`            | Standard. Run K timed repetitions per param point.       |
| `--warmup=W`          | Standard. Run W untimed warmup iterations.               |
| `--seed=…`            | Standard. Seeds the path-table PRNG, mixed with `depth` so distinct sweep points get distinct dispatch streams. |

## Reading the curve

Below the RAS cliff: per-call cost ≈ a few cycles (CALL + RET + dispatch
load + branch). Above the cliff: per-call cost steps up by the
ret-mispredict penalty.

Real example, Apple Silicon P-core via the `--core=3` (informational)
fallback path, `--depth=1..64`:

```
depth   cycles/site
   1     13.4    ← outer-loop amortization noise at very low N
   4      8.2    ← floor: pure CALL+RET cost (~2 cycles each)
   8     10.8
  16     20.1    ← gradual rise begins (RAS pressure)
  32     24.0    ← partial-miss plateau
  60     46.4    ← hard cliff (deepest predictor exhausted)
  64     45.5    ← post-cliff plateau
```

Apple Silicon shows a layered structure — a soft rise from depth ~8 and a
sharp cliff around depth 60 — rather than the single sharp step common on
Intel/AMD. Other shipping cores: Cortex-A78 RAS ≈ 24, Sunny/Golden Cove
≈ 16–32, Zen4 ≈ 32; expect the cliff at or near the documented capacity.

## Caveats specific to this benchmark

- **Frequency scaling.** Probe and benchmark must share the same governor
  state. See the project README's discipline section.
- **macOS / Apple Silicon pinning.** macOS rejects per-core pin requests;
  ferret prints a warning and falls back to a P-cluster QoS hint. Probe
  and benchmark land on *some* P-core but not necessarily the same one,
  so cycle counts are stable per-cluster, not per-core. Use
  `taskpolicy -b` or `sudo nice` if you need stronger guarantees.
- **Predictor sophistication.** The cliff height depends on how weak the
  CPU's fallback indirect predictor for `ret` is once RAS misses. On
  most cores the fallback is near-useless and the cliff is large. The
  K = 8 multi-target dispatch ensures the fallback cannot memorize a
  single correct target per ret PC, so it works even on cores with
  history-rich indirect predictors.
- **Iteration budget vs. depth.** Per-call cost is reported per site;
  default `iterations()` aims for roughly 10 ms of work per measurement
  irrespective of depth. The path-table period (`path_table_rows`)
  defaults to 4096 — long enough that no shipping predictor's history
  can memorize the dispatch pattern. Raise it if you suspect a host
  with unusually large indirect prediction history.

## Related docs

- Construction rationale: `docs/superpowers/specs/2026-05-12-ras-depth-kernel-pattern.md`
- Full benchmark design: `docs/superpowers/specs/2026-05-12-nested-call-depth-design.md`
- Implementation plan: `docs/superpowers/plans/2026-05-12-nested-call-depth.md`

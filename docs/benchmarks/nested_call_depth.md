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
| `--path_table_rows=N` | Per-iteration dispatch table row count. Default 256. Must be a power of two ≥ 2. Bigger ⇒ longer dispatch-pattern period (harder for indirect predictors to learn) but bigger memory footprint (`N × (depth+1)` bytes); once the table exceeds L1D, per-call cost inflates progressively with depth and the pre-cliff curve becomes a measurement of cache pressure rather than RAS pressure. The default keeps the table at ≤ 16 KB through depth 64 so it stays in L1D on every shipping core. Raise it if you suspect a host with very deep indirect-prediction history (TAGE > 8 bits). |
| `--freq=…`            | Standard ferret flag. Enables `cycles_per_site_*` columns. |
| `--core=…`            | Standard ferret pinning flag. Pin probe and benchmark to the same core. |
| `--reps=K`            | Standard. Run K timed repetitions per param point.       |
| `--warmup=W`          | Standard. Run W untimed warmup iterations.               |
| `--seed=…`            | Standard. Seeds the path-table PRNG, mixed with `depth` so distinct sweep points get distinct dispatch streams. |

## Reading the curve

Below the RAS cliff: per-call cost ≈ a few cycles (CALL + RET + dispatch
load + branch). Above the cliff: per-call cost steps up by the
ret-mispredict penalty.

Real example, Apple Silicon P-core, `--depth=1..64` at the default
`path_table_rows=256`:

```
depth   cycles/site
   1     11.4    ← outer-loop overhead amortized over only 2 sites
   4      7.4    ← floor: CALL + RET + dispatch (~7 cycles/pair)
   8      7.1
  16      8.3
  32      8.7    ← flat all the way to the cliff
  48      8.6
  58     10.2
  60     28.3    ← cliff: rets miss RAS, fall back to indirect predictor
  64     27.8    ← post-cliff plateau
```

The pre-cliff floor sits at ~7 cycles per call/ret pair on this core; the
cliff at depth 60 is the RAS capacity. Other shipping cores typically
show the cliff at the documented RAS capacity (Cortex-A78 ≈ 24,
Sunny/Golden Cove ≈ 16–32, Zen4 ≈ 32). Cliff height is the
ret-mispredict penalty on that core.

> ⚠️ **Path-table cache pressure.** At depths above ~32 with the original
> default of `path_table_rows=4096`, the dispatch table no longer fits in
> L1D and per-call cost inflates progressively *before* the RAS cliff —
> producing a misleading "gradual rise" that is actually L1D miss
> latency, not RAS pressure. Keep `path_table_rows` at the default unless
> you have a specific reason to raise it.

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
  defaults to 256 — long enough that no shipping predictor's history
  can memorize the dispatch pattern. Raise it if you suspect a host
  with unusually large indirect prediction history.

## Related docs

- Construction rationale: `docs/superpowers/specs/2026-05-12-ras-depth-kernel-pattern.md`
- Full benchmark design: `docs/superpowers/specs/2026-05-12-nested-call-depth-design.md`
- Implementation plan: `docs/superpowers/plans/2026-05-12-nested-call-depth.md`

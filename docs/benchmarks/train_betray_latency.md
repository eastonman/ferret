# `train_betray_latency` — conditional-branch misprediction penalty

`K` data-dependent conditional branches in a chain, branch-target =
next instruction. Each kernel call runs **one full cycle** of `M`
training rounds (all-taken at every PC) followed by **one betrayal
round** (all-not-taken at every PC). With the bimodal counter
saturated to TAKEN by training, every betrayal branch mispredicts —
giving exactly `K` mispredicts per kernel call.

Per parameter point, the benchmark builds many JIT'd kernel pairs
(B = betray fill, C = control fill) at **distinct PCs**, runs each
once, and differences the wall-clock ticks. Aggregation is
inter-quartile mean over the per-trial differences. With `--freq`
the CSV's `cycles_per_site_median` is _cycles per mispredict_;
without it, `ns_per_site_median` is _ns per mispredict_.

## Why the design looks like this

Classical train-and-betray (saturate a branch in one direction, then
flip it) **fails on modern global-history predictors** like the TAGE
variant in Apple Firestorm/M-series and recent AMD/Intel cores. The
predictor recognises the `(M+1)*K`-branch cycle after a few
iterations and starts predicting the betrayal round correctly. To
defeat this, the benchmark:

- Uses **fresh PCs per trial.** TAGE entries are keyed by
  `hash(PC, GHR)`. New PCs hit empty tables → TAGE falls back to
  bimodal, which we _can_ saturate.
- Holds every trial's `JittedKernel` alive throughout the
  measurement loop. sljit's executable allocator
  (`sljitExecAllocatorCore.c`) reuses freed blocks within its 64 KiB
  chunks, so a `build/free/build` cycle returns the same VA. Keeping
  blocks alive forces sljit to walk forward through the chunk,
  giving each trial a distinct code address.
- Pre-builds both pattern buffers (`flat_betray_`, `flat_control_`)
  once per `measure_row` call. Avoids the dangling-pointer race the
  earlier "reassign-flat-per-emit" design had with kernels' baked
  `SLJIT_IMM` buffer pointers.

## Kernel structure

```text
   PC                  site (>= spacing_bytes apart)
 0x0000   ┌──────────────────────────────────────┐
          │  MOV  r2, [row_ptr + 0]              │  load 32-bit outcome
          │  CMP  r2, 0                          │
          │  JNE  .Lnext_0                       │ ──┐  branch-to-next
          │ .Lnext_0:                            │ ◄─┘
          │  <NOP pad>                           │
          ├──────────────────────────────────────┤
          │   ... K branches total ...           │
          ├──────────────────────────────────────┤
          │  ADD  hist_idx, hist_idx, 1          │
          │  CMP  hist_idx, M+1  → wrap to 0     │
          │  SUBS iters, iters, 1; B.NE loop_top │
          └──────────────────────────────────────┘
```

- `row_ptr = flat_base + hist_idx * (K * 4)` recomputed once per
  outer iter.
- `hist_idx` wraps mod `(M+1)`; one full cycle = `M` training rounds
  followed by `1` betrayal round.
- The outer loop runs exactly `M+1` times per kernel call — one full
  cycle. Running more cycles would give TAGE entries indexed by the
  cycle's GHR signatures the chance to gain confidence and start
  predicting the betrayal correctly.
- B and C differ only in `flat[M]`: B has all-0s (betrayal), C has
  all-1s (continued training).

## Per-benchmark options

| flag                | meaning                                                                                                                                                                                   |
| ------------------- | ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `--train_iters=M`   | Training rounds per kernel call. Default `8` — enough to saturate the bimodal counter on every documented predictor, low enough that TAGE entries don't gain confidence within one trial. |
| `--spacing_bytes=N` | Minimum per-site PC stride. Min 8 (AArch64) / 6 (x86_64). Default 16.                                                                                                                     |

## CLI surface

| flag                 | meaning                                                                                                                                                            |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| `--branches=A..B`    | Geometric sweep, default `k=1`, e.g. `16384..65536`.                                                                                                               |
| `--branches=A..B@k`  | Geometric sweep with `k` samples per octave.                                                                                                                       |
| `--branches=v1,v2,…` | Explicit list.                                                                                                                                                     |
| `--train_iters=N`    | See above. Default 8.                                                                                                                                              |
| `--spacing_bytes=N`  | See above. Default 16.                                                                                                                                             |
| `--reps=N`           | Trials whose differenced ticks contribute to the IQM. Default 7 (framework). For tight measurements use `--reps=99` or higher; precision scales as `1/sqrt(reps)`. |
| `--seed=…`           | Accepted but unused (pattern is deterministic).                                                                                                                    |

See [`../cli.md`](../cli.md) for global flags.

## How to read the output

```sh
build/ferret run dependent_chain_throughput --core=3 --out=/tmp/freq.csv
python3 scripts/freq.py /tmp/freq.csv
# → estimated_freq=4.484GHz (Apple M4 P-core example)

build/ferret run train_betray_latency --core=3 --reps=99 \
    --freq=4.484GHz --out=/tmp/misp.csv

python3 scripts/plot.py line /tmp/misp.csv --out=/tmp/misp.png
```

The headline number is `cycles_per_site_median` at the largest K in
the sweep. On Apple M4 P-core this is **~15 cycles per mispredict**
(reproducible to ±0.5 cycles across reps=99 runs).

## Reading the K-sweep curve

A wider sweep `--branches=4096..131072` exposes two uarch features
in one plot:

- **Linear region (K ≤ predictor capacity)**: per-mispredict cost
  converges from ~25 cycles down to ~15 cycles as K grows. At small
  K, fixed overheads (kernel prologue/epilogue, training-round cold
  starts, JIT compile timing) inflate the differenced value. At
  large K, those overheads are a tiny fraction and the reading
  converges to the true misprediction penalty.

- **Capacity cliff (K > 2 × bimodal-table-size)**: apparent
  cost _halves_ (drops to ~7 cycles on M4 at K=131072). This is not
  a faster mispredict — it's a _lower mispredict rate_. With P
  distinct PCs sharing one bimodal table entry, only the first 2
  betrayals at that entry flip the saturating counter; subsequent
  branches at the entry get a correct prediction "for free". For P=4
  collisions per entry, rate drops to 50%; the benchmark divides
  by `K` assuming 100%, so the headline number halves.

  This cliff lets you infer the bimodal table size:

  ```text
  rate ≈ (cycles_at_high_K / cycles_at_low_K) ≈ 0.5  →  P ≈ 4  →  B ≈ K/4
  ```

  On Apple M4 this gives bimodal table ≈ 32 K entries.

A linear-regression fit of `ticks_median` vs `branches` across the
linear region gives **`c_misp − c_baseline`** directly (~12.5 cycles
on M4), with overheads cancelled in the intercept. This is the
"pipeline depth" reading you'd find in vendor pipeline diagrams.

## Caveats

- **Memory cost scales with reps × K.** Each kernel is ~K × 16 bytes;
  `total_trials = reps + 3` kernel pairs are kept alive concurrently.
  At default K=65536 reps=99 peak RSS is ~500 MB. On memory-constrained
  targets (Android, low-end VMs), reduce `--reps` first, then K.
- **First-run wobble.** First-trial cold-cache and freq-ramp effects
  make `reps=21` runs vary by ~1–2 cycles run-to-run. The internal
  3-trial warmup absorbs most of it. For ±0.5 cycle precision use
  `--reps=99`; for ±0.1 cycles, average the median across several
  invocations externally.
- **All-clamped-to-zero rows.** If every per-trial `B − C` clamps to
  zero (signal below the timer's resolution + scheduling noise), the
  benchmark logs a warn and emits a zero-cost row with `reps=0`. Bump
  K or `--reps`.
- **Apple Silicon pinning.** Same caveat as every other ferret
  benchmark — probe and target may land on different P-cores. macOS
  doesn't expose per-core pinning to userspace.

## Related docs

- Design spec:
  [`../superpowers/specs/2026-05-27-train-betray-latency-design.md`](../superpowers/specs/2026-05-27-train-betray-latency-design.md).
- Sister benchmark (capacity, not latency):
  [`branch_history_footprint`](branch_history_footprint.md).
- Project two-step workflow: [project README](../../README.md).

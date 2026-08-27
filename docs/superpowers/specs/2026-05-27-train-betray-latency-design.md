# `train_betray_latency` — Microbenchmark Design

Date: 2026-05-27 (revised 2026-05-28 after empirical pivot)
Status: as-shipped.

## 1. Goal

`train_betray_latency` is the fifth ferret microbenchmark. It measures
the **per-branch misprediction penalty** in cycles (or nanoseconds
when `--freq` is omitted) for the direction predictor of a single
conditional branch.

The headline number is one scalar per core. On Apple M4 P-core the
benchmark reports ~15 cycles per mispredict with ±0.5 cycle
run-to-run precision at default settings.

Where `branch_history_footprint` measures *whether* a predictor can
track a workload (capacity cliff), this benchmark measures *how
expensive a single failure is* once one occurs. The two are
complementary.

## 2. Why the obvious design doesn't work

The microbenchmark literature describes a "train-and-betray"
construction: pin a branch's bimodal counter in one direction by
running it many times with a consistent outcome, then flip the
outcome once. The flipped branch is guaranteed to mispredict because
the saturated counter votes the wrong way.

This works on cores with bimodal-only direction prediction (pre-2010
roughly). It **fails on modern global-history predictors** (TAGE
variants in Apple Firestorm/M-series, recent AMD Zen, Intel Lion
Cove). TAGE entries are indexed by `hash(PC, GHR_window)`; when the
benchmark runs the same `(M+1) × K`-branch deterministic cycle
repeatedly, TAGE allocates entries indexed by the cycle's GHR
signatures and after enough warmup confidently predicts both the
training rounds *and* the betrayal round. The "guaranteed" mispredict
disappears.

We saw this directly on Apple M4 with the v1 implementation:
~1 cycle/mispredict measured against an expected ~13 — i.e. TAGE
correctly predicted the betrayal almost every time.

## 3. The fresh-PC differencing design

The pivot that made this work: **every measurement trial uses kernels
at distinct code addresses (fresh PCs).** TAGE entries built by
earlier trials don't apply because the new trial's `(PC, GHR)` tuples
don't collide with prior ones. The predictor falls back to bimodal
for these fresh PCs, and bimodal *can* be saturated in 8 training
rounds — short enough that TAGE within a single trial doesn't gain
override confidence.

### Per-call kernel structure

Each `JittedKernel` runs **exactly one** train-and-betray cycle per
call: `M` training rounds + 1 betrayal round, all at the same K PCs
within that kernel.

```
outer_loop M+1 times {
  row_ptr = flat_base + hist_idx * (K * 4)
  for j in 0..K-1:
    load r2, [row_ptr + 4*j]
    cmp r2, 0
    jne next_j          // branch-to-next
  next_j:
    NOP padding to spacing_bytes
  hist_idx = (hist_idx + 1) mod (M+1)
}
```

- B kernel: `flat[0..M-1]` = all-1s (training, outcome TAKEN),
  `flat[M]` = all-0s (betrayal, outcome NOT-TAKEN, mispredicts).
- C kernel: `flat[0..M]` = all-1s (training only; no betrayal).
- Differenced: `K * (c_misp - c_baseline)` per kernel call.

Running more outer cycles per call would let TAGE allocate and
confirm entries at the same `(PC, GHR)` tuples → betrayal predicted
correctly → signal collapses. Hence `iterations() = M + 1`.

### Per-trial measurement loop

```
for t in 0..(reps + internal_warmup - 1):
  fill_mode_ = Betray;  kernels_B[t] = build()
  fill_mode_ = Control; kernels_C[t] = build()
# Hold all 2*total_trials kernels alive — see §4.
for t in 0..(reps + internal_warmup - 1):
  b_ticks = time(kernels_B[t]())
  c_ticks = time(kernels_C[t]())
  samples[t] = sat_sub(b_ticks, c_ticks)
discard first `internal_warmup` samples
drop zero samples
return inter_quartile_mean(samples)
```

The reported `ticks_median` is the IQM of the surviving samples.
Per-sample SNR scales as `K × c_misp` (signal) vs OS interrupt
inflation (~10 µs noise floor). The benchmark's default `K=65536`
puts per-trial signal at ~200 µs — well above the noise floor.

## 4. The sljit allocator trick

The "fresh PCs per trial" requirement collides with sljit's
executable allocator (`sljit_src/allocator_src/sljitExecAllocatorCore.c`):

- `sljit_malloc_exec(size)` searches a per-process free-block list
  before mmap'ing a new 64 KiB chunk. A `build/free/build` cycle on
  same-sized kernels returns the **same VA**.
- Even calling `sljit_free_unused_memory_exec()` doesn't help, since
  the OS hands back the same VA on `munmap`/`mmap` of equal-sized
  regions (verified empirically on macOS arm64).

The fix is non-invasive of sljit: **don't free until measurement
ends.** With all `total_trials` × 2 kernels alive in
`std::vector<JittedKernel>`, sljit must walk forward through its
chunk pool — each new emit lands at a fresh offset (~64-byte stride
within a chunk; full-chunk stride when chunks fill). Distinct enough
PCs that bimodal-table collisions are statistical, not deterministic.

## 5. Pattern-buffer ownership invariant

The JIT bakes `flat_.data()` as `SLJIT_IMM`. The v1 design
reassigned `flat_` inside every `emit_kernel` call, which freed the
buffer the previous kernel's code pointed at. The fix: keep two
member-field buffers, `flat_betray_` and `flat_control_`, populated
once per `measure_row` call. `emit_kernel` picks one based on
`fill_mode_` and bakes that address. The buffers outlive every
kernel built in one `measure_row` invocation.

## 6. Parameters

| flag                       | default       | role                                                                 |
| -------------------------- | ------------- | -------------------------------------------------------------------- |
| `--branches=K` (sweep)     | `16384..65536 @1` | mispredicts per kernel call. Large K is mandatory for SNR.       |
| `--train_iters=M`          | `8`           | training rounds before the betrayal. Bimodal saturates by M=8.       |
| `--spacing_bytes=N`        | `16`          | minimum per-site PC stride. Same role as in `branch_history_footprint`. |
| `--reps=N`                 | `7` (framework) | trials contributing to the IQM. Practical: 21..99. Memory scales with reps × K. |

Internal mechanism (not exposed via CLI):
- `internal_warmup = max(--warmup, 3)`. First 3 trials are discarded.
- Zero samples (B<C) are dropped before aggregation.
- Empty-after-filter rows return `reps=0`, `ticks=0`, with a `warn` log.

## 7. Framework refactor

To support a measurement that doesn't fit the single-JIT-kernel
mould, the spec adds one virtual to `Benchmark`:

```cpp
virtual MeasurementRow measure_row(const Params& p, int reps, int warmup) = 0;
```

`run_command::measure_all` calls `bench.measure_row(p, reps, warmup)`
per row instead of building a `JittedKernel` + calling `runner::measure`
directly. The previous code path is preserved as a one-line helper
`runner::single_kernel_measure(b, p, reps, warmup)`; the four
existing benchmarks delegate to it.

This split lets a benchmark with a richer measurement strategy
(differencing, multi-rep aggregation, etc.) own its measurement
without leaking into the framework.

## 8. Reading the K-sweep

The default 3-point sweep `K ∈ {16K, 32K, 64K}` is enough to verify
the headline is stable across K. A wider sweep
`--branches=4096..131072` exposes two uarch features:

1. **Convergence**: per-mispredict cost drops from ~25 cycles
   (K=4K, overhead-inflated) to ~15 cycles (K=64K, overhead
   negligible).
2. **Cliff** at K ≈ 2× bimodal-table-size: apparent cost halves
   because shared bimodal entries flip after the first 2 betrayals.
   On M4 the cliff at K=131072 implies a bimodal table of ~32K
   entries.

A linear-regression fit of `ticks_median` vs `K` across the linear
region gives `c_misp − c_baseline` directly (~12.5 cycles on M4),
overhead cancelled in the intercept.

## 9. Validation

- **K=1 emission**: smoke test — kernel builds and verifies at the
  smallest possible chain.
- **Layout snapshot**: same `verify_uniform_spacing` machinery as
  `branch_history_footprint`.
- **Sites-per-kernel = K**: the CSV's per-site normalization gives
  ns/cycles per *mispredict* (one betrayal round per kernel = K
  mispredicts per kernel call).
- **Repeatability**: 10× back-to-back invocations at default
  settings on Apple M4 P-core gave 14.4–15.2 cycles (spread 0.8).

## 10. Out of scope

- **PMU integration**. `perf_event_open` (Linux) and `kperf` (macOS,
  root) would let us count mispredicts directly and dispense with the
  K-divided-by-assumption derivation. Defer to a future benchmark
  that needs ground-truth mispredict counts.
- **Long-latency-load amplification** (Wong, stuffedcow.net) — would
  widen the mispredict resolution window but doesn't help once the
  predictor learns the cycle. Not needed given fresh-PC design works.
- **`train_iters` as a sweep axis**. Fixed at default; M=8 saturates
  bimodal on all documented predictors.

## 11. Related docs

- Sister benchmark (capacity): [`branch_history_footprint`
  spec](2026-05-17-branch-history-footprint-design.md) /
  [doc](../../benchmarks/branch_history_footprint.md).
- Per-benchmark page: [`train_betray_latency`
  doc](../../benchmarks/train_betray_latency.md).
- Original v1 plan (historical, not as-shipped):
  [implementation plan](../plans/2026-05-27-train-betray-latency.md).

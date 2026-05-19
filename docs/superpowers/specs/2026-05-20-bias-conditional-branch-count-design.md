# `bias_conditional_branch_count` — Microbenchmark Design

Date: 2026-05-20
Status: brainstorming output, pending user review before plan generation.

## 1. Goal

`bias_conditional_branch_count` is the fifth ferret microbenchmark. It
probes the **effective capacity of the Statistical Corrector (SC) bias
table** in TAGE-SC-L–style direction predictors, by constructing a
workload where TAGE alone is forced into a steady-state above the
information-theoretic mispredict floor while SC's per-PC bias counters
remain the only structure that can close the gap. Sweeping `branches`
orthogonally to the total pattern volume `total_outcomes` lets the user
test the falsifiable claim that the capacity cliff is **PC-count
driven** (i.e., SC bias entries) rather than **history-volume driven**
(i.e., TAGE tagged tables).

The benchmark sits next to `branch_history_footprint` in the
direction-predictor family: same kernel shape, different experimental
design and a different microarchitectural structure under test.

### 1.1 Why this is hard, and why this design works

A naive "biased branch with random history pollution" benchmark does
**not** distinguish SC from TAGE, because TAGE's own base predictor
(T0, the bimodal table) is itself a per-PC bias counter. For a 95/5
branch, T0 saturates at strong-T on its own; tagged tables don't
allocate (no usefulness gain over T0 on i.i.d. data); the steady-state
mispredict rate sits at ~5% with or without SC.

To create a measurable SC-vs-TAGE gap, the workload must:

1. **Force TAGE's tagged tables to override T0 most of the time** — so
   that T0's correct bias signal is suppressed by noisy tagged
   predictions. This is achieved by making the per-branch outcome
   stream a long aperiodic Bernoulli(p) sequence with GHR that varies
   enough for tagged tables to allocate but not enough for any tagged
   entry to saturate. The "rotation of NT positions every round"
   framing is equivalent to "the pattern period exceeds TAGE's
   effective history horizon," which we implement with a single long
   pre-generated random buffer.

2. **Create destructive aliasing in SC's bias table when capacity is
   exceeded** — so the cliff position has a measurable signature. This
   requires *opposing* biases: if all branches bias toward the same
   direction, aliased SC entries still agree on the prediction and the
   cliff never appears. The design splits branches into two
   sub-populations: a fraction biased toward T (Bernoulli(p)), the
   complement biased toward NT (Bernoulli(1-p)). With a default 50/50
   split, every aliased pair in the SC bias table contains one of each
   direction, driving the saturated counter back toward zero and
   producing ~50% mispredict on the aliased PCs.

3. **Isolate SC bias capacity from TAGE tagged-table capacity** — so
   the cliff can be attributed to SC specifically. SC's bias table is
   indexed by PC alone; its pressure scales with `branches`. TAGE's
   tagged tables are indexed by `PC ⊕ hash(history)`; their pressure
   scales with `branches × pattern_period = total_outcomes`. Sweeping
   the two axes orthogonally means: if the cliff in `branches` occurs
   at the same x-position across all `total_outcomes` rows, the cliff
   is PC-count driven (SC). If the cliff slides with `total_outcomes`,
   the cliff is history-volume driven (tagged) and the SC-isolation
   claim is falsified.

The 2D surface plot landed in `2026-05-18-surface-plot-design.md` is
the natural visualization: cycles/site as height over the
`(branches, total_outcomes)` plane. A vertical wall (cliff invariant
to `total_outcomes`) is the SC bias-table fingerprint.

## 2. Scope

### In scope

- Two sweep axes: `branches` and `total_outcomes`.
- Derived per-point quantity: `pattern_period = total_outcomes /
  branches` (integer division, floored, with `>= 1` enforced).
- Static branch chain emitted via the **shared site emitter** factored
  out from `branch_history_footprint`. Each site is the same
  load + cmp + branch-to-next shape; the only difference is the
  pattern fill strategy.
- Per-branch direction assignment (T-biased vs NT-biased), deterministic
  given `--seed`.
- Per-bench options: `bias_pct` (magnitude of per-branch bias),
  `nt_branch_pct` (percentage of branches assigned NT-preferred),
  `spacing_bytes` (per-site PC stride).
- `uint32_t`-per-outcome storage (kernel shared with
  `branch_history_footprint`), with documented data-cache caveat at
  the high end of `branches`.
- x86_64 and AArch64.
- Unit + integration tests, mirroring the existing direction-predictor
  benchmark's test layout.

### Out of scope

- Bit-packed or `uint8_t`-per-outcome storage. The compression would
  push the L1d ceiling on per-row footprint higher (allowing larger
  `branches`), but it requires changing the per-site instruction
  sequence (load byte + AND + cmp instead of load word + cmp), which
  diverges from the shared emitter. Promotable to v2 once the v1 cliff
  position is empirically known.
- Sweeping `nt_branch_pct` or `bias_pct` as Cartesian axes. They are
  per-run options in v1. The diagonal-isolation experiment uses fixed
  values; sensitivity studies are run as separate invocations and
  overlaid.
- Defeating T0 by PC-low-bit aliasing (Route 1 from brainstorming).
  Different experimental design, separate benchmark. This benchmark
  uses the "perpetual tagged-table re-allocation" mechanism instead.
- The Loop predictor, IMLI table, or other SC sub-components beyond
  the bias table. The bias table is the largest and most
  capacity-revealing SC component; other components are smaller and
  specialized.
- A "TAGE-only" reference curve. We have one CPU; the falsifiable
  claim is on the *shape* of the surface (cliff invariance to
  `total_outcomes`), not a side-by-side compare.

## 3. Workflow

Same two-step pattern as the other benchmarks, with the new surface
renderer:

```sh
# Step 1: probe core frequency (existing benchmark, unchanged).
build/ferret run dependent_chain_throughput --core=3 --out=/tmp/freq.csv
python3 scripts/freq.py /tmp/freq.csv
# → estimated_freq=4.521GHz

# Step 2: sweep branches × total_outcomes at the default bias.
build/ferret run bias_conditional_branch_count --core=3 \
    --branches=1..8192 --total_outcomes=8192..1048576 \
    --bias_pct=95 --nt_branch_pct=50 \
    --freq=4.521GHz --out=/tmp/sc.csv

# Step 3: 3D surface plot (the load-bearing visualization).
python3 scripts/plot.py surface /tmp/sc.csv --out=/tmp/sc.png

# Or a 2D heatmap for quick inspection:
python3 scripts/plot.py heatmap /tmp/sc.csv --out=/tmp/sc-heat.png

# Or a 1D slice at fixed total_outcomes:
python3 scripts/plot.py line /tmp/sc.csv --x=branches \
    --filter=total_outcomes=65536 --out=/tmp/sc-slice.png
```

The fingerprint to look for: the cliff position along `branches` is
*invariant* to `total_outcomes`. A vertical wall in the surface.

## 4. Kernel construction

### 4.1 Memory layout

Identical to `branch_history_footprint`: a single flat `uint32_t`
buffer interpreted as `flat[pattern_period][branches]`, transposed so
consecutive branch sites in one outer iteration access adjacent words.

Buffer size = `total_outcomes * 4` bytes. At the default-axis max
(`total_outcomes = 2^20 = 1 048 576`) that's 4 MB — fits L2 on every
target. Crucially, the buffer size depends only on `total_outcomes`,
not on the `(branches, pattern_period)` split. The diagonal-constraint
sweep holds buffer footprint constant while varying `branches`, so any
cliff observed *cannot* be attributed to data-cache effects on
buffer-size.

The per-row footprint **does** depend on `branches`: row size =
`branches * 4` bytes. At `branches = 8192` (default axis max) that's
32 KB — at the L1d ceiling on most current cores. Caveat documented in
§11.

### 4.2 Per-arch site bytes

Inherited verbatim from `branch_history_footprint` §4.2. The per-site
instruction sequence and `kMinSiteBytes` / `kBranchAlign` constraints
are unchanged.

### 4.3 Kernel skeleton

Conceptually identical to `branch_history_footprint` §4.3. The only
JIT-immediate that changes between benchmarks is `iters` (a function
of `branches`, not of `bias_pct` / `nt_branch_pct`). The bias and
direction-assignment knobs influence only the *contents* of `flat_`,
not the emitted code.

### 4.4 Branch-to-next-instruction trick

Inherited verbatim from `branch_history_footprint` §4.4.

### 4.5 Row pointer update per iteration

Inherited verbatim from `branch_history_footprint` §4.5, with
`history_len` substituted by `pattern_period`. The runtime computation
is `row_ptr = flat_base + hist_idx * branches * 4` recomputed once per
outer iteration; `hist_idx` wraps at `pattern_period`.

### 4.6 Code sharing strategy

The site-emission logic in `branch_history_footprint.cpp` is extracted
into a shared helper exposed via a header:

```cpp
// include/ferret/branch_chain_emit.hpp
namespace ferret {

struct BranchChainEmitConfig {
    const uint32_t* flat_base;
    size_t branches;
    size_t pattern_period;
    size_t spacing_bytes;
    size_t iterations;
};

struct BranchChainEmitResult {
    std::vector<sljit_label*> site_labels;  // size == branches + 1
};

BranchChainEmitResult emit_branch_chain(sljit_compiler*,
                                         const BranchChainEmitConfig&);

}  // namespace ferret
```

Both benchmarks call this helper. The differences live in:

- The pattern-fill routine (per-benchmark `generate_pattern_fill`).
- The set of per-bench options exposed via `options()`.
- The `axes()` returned by each benchmark.
- The reading-the-curves doc page.

The refactor preserves `branch_history_footprint`'s existing observable
behavior (same opcodes, same layout, same CSV columns). A regression
guard in `tests/test_branch_history_footprint.cpp` re-asserts that the
post-refactor opcode bytes match the pre-refactor opcode bytes for a
small `(branches=4, history_len=4)` point.

## 5. Sweep axes and benchmark options

### 5.1 Axes

```cpp
SweepAxes axes() const override {
  return {
      Axis::geom_range("branches",        1,        1 << 13, /*k=*/1),  // 1..8192
      Axis::geom_range("total_outcomes",  1 << 13,  1 << 20, /*k=*/1),  // 8192..1048576
  };
}
```

Default Cartesian = 14 × 8 = 112 points. Typical run time ≈ 5 min
with the standard 10-rep schedule.

The `total_outcomes` lower bound is set to `2^13` so it equals the
`branches` upper bound — every default Cartesian point satisfies
`total_outcomes >= branches` (i.e., `pattern_period >= 1`). This
matters because `run_command.cpp::measure_rows` aborts the whole
sweep with a single `std::nullopt` when any point throws; the design
must keep the default grid validation-clean. Users widening
`--branches=...` past `8192` must also widen `--total_outcomes=...`
correspondingly to avoid invalidation.

The first axis `branches` varies slowest in the CSV (per ferret's
column-order convention), matching `branch_history_footprint`.

### 5.2 Per-bench options

```cpp
BenchOptions options() const override {
  return {
      BenchOption{.name = "bias_pct",       .default_value = 95},
      BenchOption{.name = "nt_branch_pct",  .default_value = 50},
      BenchOption{.name = "spacing_bytes",  .default_value = 16},
  };
}
```

- `--bias_pct=N`: percentage probability that a branch outputs its
  *preferred* direction on each visit. `bias_pct=50` is unbiased
  (random); `bias_pct=100` is constant. Default 95.
- `--nt_branch_pct=N`: percentage of branches assigned NT-preferred.
  The complement is T-preferred. Default 50 (even split, maximizes
  destructive aliasing). `nt_branch_pct=0` recovers the single-
  direction case; `nt_branch_pct=100` is the same scenario mirrored;
  intermediate values yield asymmetric aliasing.
- `--spacing_bytes=N`: per-site PC stride. Validated against
  `kMinSiteBytes` and `kBranchAlign` as in `branch_history_footprint`.

All options are int-typed because ferret's `BenchOption` carries an
`int64_t` default. Percentage encoding (0..100) avoids float
plumbing through the CLI / CSV.

### 5.3 `sites_per_kernel` and `iterations`

```cpp
size_t sites_per_kernel(const Params& p) const override {
  return p.get<size_t>("branches");
}

size_t iterations(const Params& p) const override {
  return std::max<size_t>(1, 10'000'000 / p.get<size_t>("branches"));
}
```

Per-site cost = ticks / (iterations × branches). The runner converts
to cycles when `--freq` is set, nanoseconds otherwise.

## 6. Class shape

```cpp
struct BiasConditionalBranchCount : Benchmark {
  std::vector<uint32_t> flat_;
  std::vector<sljit_label*> last_labels_;
  size_t last_branches_ = 0;
  size_t last_spacing_ = 0;

  std::string name() const override { return "bias_conditional_branch_count"; }
  SweepAxes  axes()    const override;
  BenchOptions options() const override;
  size_t sites_per_kernel(const Params&) const override;
  size_t iterations(const Params&) const override;
  void emit_kernel(sljit_compiler*, const Params&) override;
  void verify_layout(sljit_compiler*) override;
};

FERRET_BENCHMARK("bias_conditional_branch_count", BiasConditionalBranchCount);
```

`emit_kernel` is responsible for: validating params, deriving
`pattern_period`, resizing+filling `flat_` for the current
`(branches, total_outcomes, bias_pct, nt_branch_pct)` point, then
calling `emit_branch_chain` (§4.6) with the configured immediates.

## 7. Pattern generation

This is the only structural novelty vs. `branch_history_footprint`.

### 7.1 Direction assignment

Each of the `branches` PCs is assigned a *preferred* outcome
direction (T or NT). The assignment is deterministic given `--seed`
and the current `(branches, total_outcomes)` point: we shuffle the
indices `[0, branches)` with a seeded `std::mt19937_64` and take the
first `branches * nt_branch_pct / 100` indices as NT-preferred. This
gives a spatially-mixed assignment (avoids stripe patterns like
"all even indices NT") which keeps GHR contributions from adjacent
sites balanced.

### 7.2 Outcome fill

For each `(t, j)` in the flat buffer:

```cpp
bool prefers_nt = nt_set.contains(j);                       // §7.1
uint32_t prob_taken_q14 = prefers_nt
    ? (100 - bias_pct) * 16384 / 100
    : bias_pct           * 16384 / 100;
uint32_t r = static_cast<uint32_t>(rng() & 0x3fff);         // 14-bit
flat_[t * branches + j] = (r < prob_taken_q14) ? 1u : 0u;
```

The 14-bit fixed-point comparison is exact for integer `bias_pct ∈
[0, 100]`, avoids floats, and keeps the RNG draw cheap. The outcome
is `1` when the branch *should be taken* and `0` otherwise — matching
the existing kernel's contract that `cbnz`/`jecxz` branch on nonzero.

### 7.3 Seed mixing

Two independent RNG streams to keep direction-assignment **stable
across `total_outcomes` at fixed `branches`** (so a vertical slice
through the surface holds the per-branch direction set fixed):

```cpp
// Direction assignment — independent of total_outcomes and bias_pct.
uint64_t dir_seed = static_cast<uint64_t>(seed)
                  ^ (static_cast<uint64_t>(branches)        * 0x9E3779B97F4A7C15ULL)
                  ^ (static_cast<uint64_t>(nt_branch_pct)   * 0xD6E8FEB86659FD93ULL);
std::mt19937_64 rng_dir(dir_seed);
// ... shuffle [0, branches) and take first nt_count as NT-preferred ...

// Outcome fill — uses full mix.
uint64_t fill_seed = dir_seed
                   ^ (static_cast<uint64_t>(total_outcomes)  * 0xBF58476D1CE4E5B9ULL)
                   ^ (static_cast<uint64_t>(bias_pct)        * 0x94D049BB133111EBULL);
std::mt19937_64 rng_fill(fill_seed);
// ... fill flat_ ...
```

Distinct grid points and distinct option values get distinct fills.
Three new mix constants (different odd 64-bit primes) are added to
keep the convention extensible.

## 8. CSV impact

`bias_conditional_branch_count` adds two axis columns (`branches`,
`total_outcomes`) and three option columns (`bias_pct`,
`nt_branch_pct`, `spacing_bytes`) to the right of `benchmark`:

```
benchmark,branches,total_outcomes,bias_pct,nt_branch_pct,spacing_bytes,ticks_min,ticks_median,iterations,sites_per_kernel,cycles_per_site,ns_per_site
bias_conditional_branch_count,1,4096,95,50,16,...
bias_conditional_branch_count,1,8192,95,50,16,...
...
```

No plot script changes needed. `scripts/plot.py heatmap`, `line`, and
`surface` already operate on the generic axis-column convention.

## 9. Error handling

Pre-codegen validation in `emit_kernel`, thrown as
`std::invalid_argument` *before* any sljit calls:

- `branches < 1` → "branches must be >= 1".
- `total_outcomes < 1` → "total_outcomes must be >= 1".
- `total_outcomes < branches` → "total_outcomes (X) must be >=
  branches (Y) so pattern_period >= 1".
- `bias_pct < 0 || bias_pct > 100` → "bias_pct must be in [0, 100]".
- `nt_branch_pct < 0 || nt_branch_pct > 100` → "nt_branch_pct must
  be in [0, 100]".
- `spacing_bytes < kMinSiteBytes` → same message as
  `branch_history_footprint`.
- `spacing_bytes % kBranchAlign != 0` → same message.

Post-codegen verification in `verify_layout`: inherited from the
shared emitter (§4.6).

Buffer-allocation failure throws `std::bad_alloc`; propagated to the
runner. At the default-axis max the buffer is 4 MB, well within any
plausible RAM budget.

## 10. Testing

Mirrors `branch_history_footprint`'s test suite under
`tests/test_bias_conditional_branch_count.cpp`.

### 10.1 Unit tests

- **Registry**: `bias_conditional_branch_count` resolves via
  `BenchmarkRegistry::create`.
- **Axes**: `axes()` returns two `geom_range` axes with names
  `branches`, `total_outcomes` and the expected `(lo, hi, k)`.
- **Options**: `options()` returns `bias_pct`, `nt_branch_pct`,
  `spacing_bytes` with documented defaults.
- **Validation**: invalid combinations from §9 throw
  `std::invalid_argument` with the documented message prefixes.
  Edge case `total_outcomes == branches` is accepted (yields
  `pattern_period = 1`).
- **Determinism of fill**: same `--seed` + same
  `(branches, total_outcomes, bias_pct, nt_branch_pct)` produces
  identical `flat_` contents.
- **Direction-assignment stats**: with `nt_branch_pct=50, branches=
  1024`, the assignment contains exactly 512 NT-preferred indices.
  Test this with a public accessor on the benchmark instance
  (`direction_view()` returning a `std::span<const uint8_t>` of
  0/1 per branch).
- **Outcome-distribution stats**: with `bias_pct=95, nt_branch_pct=
  50, branches=64, total_outcomes=65536` (pattern_period=1024), the
  per-branch outcome frequency on T-preferred branches lies in
  [0.93, 0.97] and on NT-preferred branches in [0.03, 0.07] (3-sigma
  bounds with `pattern_period=1024` samples).
- **Site layout**: emit at `(branches=4, total_outcomes=16,
  spacing=16)`, generate code, assert `verify_layout` accepts and
  that site spacing matches.

### 10.2 Integration test

Smoke-run with very small axes (`branches=1..4`,
`total_outcomes=8..32`) and assert:

- CSV has 3 × 3 = 9 rows with the expected columns.
- All `ticks_min > 0`.
- The `bias_pct=50, nt_branch_pct=50` baseline (effectively uniform
  random) produces cycles/site close to the
  `branch_history_footprint --pattern=1` value at the same
  `(branches, history_len)` point — confirming the kernel shape is
  unchanged.

### 10.3 Sanitizer matrix

ASAN, UBSAN, TSAN clean — same matrix as other benchmarks. Highest-
value targets: the new direction-assignment shuffle (off-by-one in
the NT-set boundary), the fixed-point probability comparison
(off-by-one at `bias_pct=0`/`100`).

### 10.4 Manual platform check

Before merging, run the full default sweep on both supported arches
and confirm:

- A floor region at small `branches` (cycles/site near the
  `(1-p)·penalty + base` analytical floor).
- A cliff at some intermediate `branches` value.
- The cliff position is **invariant to `total_outcomes`** to within
  ±1 octave on the geometric axis (i.e., a vertical wall in the
  surface, not a diagonal ridge).

If the cliff diagonally tracks `total_outcomes`, the diagonal-
isolation claim is falsified for this CPU and the docs page must
flag the result as "tagged-table-pressure-dominated, SC bias capacity
not directly resolvable on this core."

## 11. Reading the curves

The 3D surface (`scripts/plot.py surface`) plots cycles/site as
height over `(branches, total_outcomes)`. The expected shape is an
**L-shaped high-cycles region** occupying the upper-right corner of
the plane, bounded by two cliffs:

1. **Lower-left plateau (low `branches`, low `total_outcomes`)** —
   cycles/site at the predictor-memorized floor. Either pattern is
   short enough for tagged tables to memorize (low `total_outcomes`)
   or the PC set is small enough for SC bias to handle it (low
   `branches`).
2. **Cliff along `total_outcomes` at fixed high `branches`** —
   horizontal wall. Crossing this cliff means the per-PC pattern
   period exceeds what tagged tables can memorize; tagged tables
   start churning. Cliff position ≈ tagged-table effective
   memorization threshold for this `branches` value.
3. **Cliff along `branches` at fixed high `total_outcomes`** —
   vertical wall. **This is the SC-bias-table-capacity signature.**
   Below the cliff, SC bias entries are unaliased; above, opposite-
   bias PCs collide in SC slots and the saturated counter is pulled
   toward zero → mispredicts at the aliased subset → cycles climb.
4. **Upper-right plateau (high `branches`, high `total_outcomes`)** —
   cycles/site at the high-mispredict ceiling. Neither predictor can
   help: tagged tables churn, SC bias aliases destructively.

The falsifiable SC-isolation claim is on cliff #3 specifically: its
position along `branches` should be **invariant to `total_outcomes`**
within the range above cliff #2 (the regime where TAGE is no longer
memorizing). If cliff #3 instead slides diagonally with
`total_outcomes`, the cliff isn't pure SC-capacity — tagged-table
pressure is contributing, and the SC reading is confounded for this
CPU.

The cleanest 1D reduction is a `--filter=total_outcomes=N` line slice
at any `N` well above the tagged-table threshold (e.g., the largest
sampled `total_outcomes`). Plot cycles/site vs `branches`; read off
the cliff x-position as the SC bias-table effective entry count.

### 11.1 What changes when `bias_pct` or `nt_branch_pct` change

- `bias_pct` controls the floor height: floor ≈
  `(1 - bias_pct/100) · penalty + base`. Re-running with
  `bias_pct ∈ {70, 80, 90, 95, 99}` and overlaying line slices gives
  a family of curves with the same cliff position but different floor
  heights.
- `nt_branch_pct` controls the height of the post-cliff plateau:
  with `nt_branch_pct=0` (all T-preferred), aliasing is constructive
  and cliff #3 disappears (T0/SC entries that alias still agree on
  T). At `nt_branch_pct=50` (default), cliff #3 is maximal.
  Intermediate values produce intermediate cliffs — a useful
  sanity-check that the destructive-aliasing mechanism is what's
  driving the signal.

### 11.1 What changes when `bias_pct` or `nt_branch_pct` change

- `bias_pct` controls the floor height: floor ≈
  `(1 - bias_pct/100) · penalty + base`. Re-running with
  `bias_pct ∈ {70, 80, 90, 95, 99}` and overlaying line slices gives
  a family of curves with the same cliff position but different floor
  heights.
- `nt_branch_pct` controls the height of the post-cliff plateau:
  with `nt_branch_pct=0` (all T-preferred), aliasing is constructive
  and the cliff disappears. At `nt_branch_pct=50` (default), the
  cliff is maximal. Intermediate values produce intermediate cliffs
  — a useful sanity-check that the destructive-aliasing mechanism is
  what's driving the signal.

## 12. Known limits

- **Per-row L1d footprint at high `branches`.** With `uint32_t`
  outcomes, the per-row footprint is `branches * 4` bytes. At
  `branches = 8192` that's 32 KB — at or just past the L1d ceiling
  on most current cores. Cycles/site beyond that point may reflect
  *combined* SC-bias-cliff and L1d-miss effects. The cliff position
  is typically below this threshold on shipping cores (SC bias
  tables are 1K–16K entries; the cliff usually lands at 1K–8K),
  but the caveat is documented and v2 may switch to a `uint8_t` or
  bit-packed encoding to push the limit higher.
- **T0 capacity may produce a secondary cliff.** TAGE's base
  bimodal table is also PC-indexed. When `branches` exceeds T0
  capacity (~4K on many cores), T0 also aliases. SC bias *can*
  correct T0 alias when T0 fires (only as fallback), so the
  observable cliff is the SC capacity. If SC bias is smaller than
  T0, the dominant cliff is SC; if SC bias is larger than T0, there
  may be a small secondary step at T0 capacity before the main SC
  cliff. Document and interpret per-CPU.
- **Other SC sub-components.** SC has GHIST / PATH / IMLI tables
  beyond BIAS. They may partially correct what BIAS cannot, blurring
  the cliff. The aperiodic, history-independent design minimizes the
  contribution of GHIST/PATH; IMLI is loop-iteration-correlated and
  doesn't engage on this workload.
- **Predictor noise floor.** The min-of-K rep schedule mitigates
  but does not eliminate run-to-run variance. Standard caveat.
- **Outer-loop tax at `branches=1`.** Same as
  `branch_history_footprint`. The leftmost column is a tax baseline.
- **Apple Silicon pinning.** Inherits the per-cluster-not-per-core
  caveat from the project README.
- **Single-CPU experiment.** No TAGE-only reference. The falsifiable
  claim is on the *shape* of the surface (cliff invariance to
  `total_outcomes`), interpreted per-CPU.

## 13. Future work (out of v1)

- `uint8_t` or bit-packed outcome storage to push the per-row L1d
  ceiling higher and allow `branches` sweeps to 64K+, exposing SC
  bias tables of any plausible size on shipping cores.
- Companion benchmark `t0_bias_collision` (Route 1 from
  brainstorming) — defeat T0 by PC-low-bit collision and read SC's
  rescue behavior. Different experimental design, separate benchmark.
- `bias_pct` and `nt_branch_pct` as Cartesian sweep axes for a
  full 4D study (`branches × total_outcomes × bias × nt_split`).
  v1 keeps them as per-run options to stay within ferret's
  established 2D-axis convention.
- Per-branch bias from a continuous distribution (e.g., Beta(α, β))
  rather than binary T-or-NT-preferred, to study how the cliff
  changes when biases are graded rather than bimodal.

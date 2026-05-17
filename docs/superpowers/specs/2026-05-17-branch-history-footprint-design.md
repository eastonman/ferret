# `branch_history_footprint` — Microbenchmark Design

Date: 2026-05-17
Status: brainstorming output, pending user review before plan generation.

## 1. Goal

`branch_history_footprint` is the fourth ferret microbenchmark. It
emits `N` data-dependent conditional branches inside an outer loop, and
sweeps both `N` (number of distinct conditional branches) and
`history_len` (per-branch pattern period). Each branch's taken/not-
taken outcome is read from a per-branch `uint32_t` array indexed by a
history position that cycles `0..history_len-1` once per outer
iteration. The user plots per-branch cost on a 2D `(branches,
history_len)` grid and reads off the **direction-predictor capacity
cliff**: per-branch cost is flat while the predictor can track all
`branches × history_len` patterns and steps up once it can't.

Per ferret's naming convention (`2026-05-09-ferret-design.md` §5.1) the
benchmark is named after the workload — *conditional branches with
per-branch histories* — not the structure it reveals (PHT / TAGE /
perceptron entries). The footprint suffix matches `direct_branch_-
footprint`: same family, different predictor.

Reference implementations the design is adapted from:

- `jiegec/cpu-micro-benchmarks/src/bp_size_gen.cpp` — kernel shape,
  cbnz/jecxz-to-next-instruction trick, data-driven branch directions.
- `ChipsandCheese/Microbenchmarks/AsmGen/tests/BranchHistoryTest.cs`
  and `DataFiles/GccBranchHistFunction.c` — the random vs zero fill
  pattern, the (branches × history) 2D sweep idea.

## 2. Scope

### In scope

- Two sweep axes: `branches` and `history_len`.
- Static branch chain constructed at JIT time using sljit + hand-
  emitted op_custom sequences. Each site is `ldr/mov + cbnz/jecxz`
  with the branch target = the next instruction (the "branch-to-next"
  trick). Address for each site's pattern load is `row_ptr +
  baked_j_disp` — one load per site, no pointer chasing.
- Single flat `uint32_t flat_[history_len][branches]` buffer
  (transposed layout — see §4 / §7), filled with random 0/1 once per
  `(branches, history_len)` point. Buffer base baked into the JIT as
  `SLJIT_IMM`; runner still calls `fn()` with zero args.
- One per-benchmark option: `pattern={random,zero}` (default `random`).
- One per-benchmark option: `spacing_bytes=N` (default 16, fixed —
  not a sweep axis in v1).
- x86_64 and AArch64 (ferret's supported platforms).
- Unit + integration tests, same pattern as `direct_branch_footprint`.

### Out of scope

- `spacing_bytes` as a sweep axis. Fixed value only in v1; user can
  override via `--spacing_bytes=N` but per-bench option semantics —
  not a Cartesian dimension. Promotable to an axis later if a
  spacing/aliasing study is needed.
- Biased random fills (`bernoulli_p`), short repeating cycles
  (`cycle:k`), or other pattern generators beyond `random` and `zero`.
- A generalized "kernel-takes-arguments" extension to ferret's
  `Benchmark` base class. We bake the data pointer as an immediate;
  the runner still calls `fn()` with zero args. If/when a second
  benchmark needs runtime data, that's the right time to add a typed
  signature mechanism.
- Indirect branches, switch tables, or other branch types. PHT/TAGE
  for indirect branches is a separate microarchitectural structure
  and warrants a separate benchmark.

## 3. Workflow

Same two-step pattern as the other benchmarks:

```sh
# Step 1: probe core frequency (existing benchmark, unchanged).
build/ferret run dependent_chain_throughput --core=3 --out=/tmp/freq.csv
python3 scripts/freq.py /tmp/freq.csv
# → estimated_freq=4.521GHz

# Step 2: sweep branches × history_len, pinned to the same core.
build/ferret run branch_history_footprint --core=3 \
    --branches=1..512 --history_len=4..4096 \
    --freq=4.521GHz --out=/tmp/bhf.csv

# Step 3: 2D heatmap (branches × history_len, cycles per branch as color).
python3 scripts/plot.py heatmap /tmp/bhf.csv --out=/tmp/bhf.png

# Or line plot with one axis as the legend:
python3 scripts/plot.py line /tmp/bhf.csv --x=branches --out=/tmp/bhf-by-branches.png
```

## 4. Kernel construction

### 4.1 Memory layout

The pattern storage is a single flat `uint32_t` buffer interpreted as
`flat[history_len][branches]` — **transposed** vs. the reference
(jiegec / chips&cheese) which stores `arr[branch][history]` and chases
two dependent loads per site. The transposed layout means:

- Consecutive branch sites in one outer iteration access adjacent
  words (`flat[hist_idx][0..branches-1]`), one L1 line per 16 sites —
  HW prefetcher–friendly sequential pattern.
- The per-branch displacement (`j * 4`) is a compile-time constant
  baked into each site's load encoding. No address arithmetic at run
  time; the address is `row_ptr + immediate`.
- `row_ptr = flat_base + hist_idx * branches * 4` is computed once per
  outer iteration and held in a callee-saved register across the
  branch chain.

Buffer size = `branches * history_len * 4` bytes. At default-axis max
(`branches=512`, `history_len=4096`) that's 8 MB — fits L2 on every
target; smaller points fit L1.

### 4.2 Per-arch site bytes

| arch    | core site instructions                       | core bytes | min spacing |
| ------- | -------------------------------------------- | ---------: | ----------: |
| AArch64 | `ldr w13, [x14, #(j*4)]` ; `cbnz w13, .Lnxt` |          8 |           8 |
| x86_64  | `mov ecx, [r14 + DISP32_J]` ; `jecxz .Lnxt`  |        8-9 |           9 |

x86_64 site: 6 B `mov` with disp32 forced (or 7 B if base needs REX) +
2 B `jecxz rel8=0`. The disp32 is forced even when `j*4` would fit in
disp8 so all sites have identical encoding length — fixed-stride
layout, like `direct_branch_footprint`'s discipline.

AArch64 site: 4 B `ldr (immediate)` with unsigned 12-bit scaled
offset (`j*4 ≤ 16380` → `j ≤ 4095`; default-axis max `j=511` is well
inside this bound) + 4 B `cbnz` with `imm19=1` targeting the next
instruction.

`spacing_bytes` defaults to **16**. Per-arch validation in `emit_kernel`:

- `spacing_bytes >= kMinSiteBytes` (8 on AArch64, 9 on x86_64).
- `spacing_bytes % kBranchAlign == 0` (1 on x86_64, 4 on AArch64) —
  same constraint `direct_branch_footprint` enforces.

Padding is emitted via the existing `padding.hpp` helper (`emit_nops`).

### 4.3 Kernel skeleton (AArch64, conceptual)

```
       MOV  x_flat_base, FLAT_BASE_IMM   ; flat[0][0] address (immediate)
       MOV  w16, 0                       ; w16 = hist_idx
       MOV  x_row_stride, ROW_STRIDE_IMM ; branches * 4 (immediate)
       MOV  x_hist_len,  HISTORY_LEN_IMM ; history_len     (immediate)
       MOV  x0, iters                    ; outer-loop counter
 loop_top:
       ; row_ptr = flat_base + hist_idx * branches * 4
       MADD x14, x16, x_row_stride, x_flat_base
   ; ---- site 0 ----
       LDR  w13, [x14, #0]               ; load flat[hist_idx][0]
       CBNZ w13, .Lnext_0                ; branch to next instr (BTB-trivial)
 .Lnext_0:
       <NOP padding to spacing_bytes>
   ; ---- site 1 ----
       LDR  w13, [x14, #4]
       CBNZ w13, .Lnext_1
 .Lnext_1:
       <NOP padding>
   ; ... branches sites total, ldr disp = j*4 ...
   ; ---- end of chain ----
       ADD  w16, w16, 1
       CMP  w16, x_hist_len
       CSEL w16, wzr, w16, EQ            ; w16 = (w16 == hist_len) ? 0 : w16
       SUBS x0, x0, 1
       B.NE loop_top
       RET
```

x86_64 mirror: at function entry, materialize `flat_base` into a
callee-saved register (`r14` in this sketch). Per outer iteration,
compute `row_ptr = flat_base + hist_idx * branches * 4` using
`lea`/`shl` (branches is power-of-2 in default axes; non-pow-2 path
uses `imul` with a stride immediate). Each site is then
`mov ecx, [row_ptr + DISP32_J]` (force-disp32 encoding) + `jecxz`.

Arch register names above are illustrative. The actual emit code
binds working values to sljit's callee-saved handles (`SLJIT_S0`,
`SLJIT_S1`, …) so sljit's register allocator doesn't reassign them
mid-kernel. Hand-emitted `op_custom` byte sequences then encode the
underlying arch registers that sljit maps those handles to — the
mapping is queried via sljit headers and recorded next to the byte
tables in the source so future readers can audit the encodings.

### 4.4 Branch-to-next-instruction trick

The conditional branch target is the immediately-following label. Both
taken and not-taken paths converge to the next site, so:

- BTB target prediction is trivial (one target per site).
- Architecturally a no-op — control falls through either way.
- The **direction predictor still has to make a call**. A
  misprediction triggers a pipeline flush even though the resolved
  target is "the next insn." This isolates BPU direction-prediction
  cost from BTB target-prediction cost.

### 4.5 Row pointer update per iteration

`row_ptr` (held in a callee-saved register) is recomputed once at the
top of every outer iteration as `flat_base + hist_idx * branches * 4`.
Within an iteration it's loop-invariant — every site uses the same
`row_ptr` plus a baked per-branch displacement, so site code never
mutates the address-base register. The history index advances by 1
per outer iteration and wraps at `history_len` via `csel`/`cmov`.

### 4.6 sljit emission strategy

Mirrors `direct_branch_footprint`:

- AArch64: the per-site `ldr (immediate)` with disp = `j*4` and the
  `cbnz` to the immediately-following label are both hand-emitted via
  `sljit_emit_op_custom` with exact 4-byte encodings. We don't depend
  on sljit's reduce-pass to keep the cbnz at 4 bytes.
- x86_64: the per-site `mov ecx, [base + disp32]` is hand-emitted to
  force the disp32 form (so all sites are identical length even when
  `j*4` would fit disp8). `jecxz` is emitted as the literal `E3 00`
  byte pair — rel8 = 0 targets the next instruction. The chain prologue
  / row_ptr update uses sljit primitives.
- `verify_layout()` runs post-codegen and asserts each site sits at
  `base + i * spacing_bytes`; same shape as `direct_branch_footprint`.
  Unlike `direct_branch_footprint`, no displacements need patching
  post-emit: both arches encode the "branch-to-next" target at emit
  time. On x86_64, `jecxz` with `rel8 = 0` targets `PC + 2` (the byte
  after the 2-byte `jecxz` encoding), which is the next instruction.
  On AArch64, `cbnz` with `imm19 = 1` targets `PC + 4` (the byte after
  the 4-byte `cbnz` encoding), which is the next instruction. Both
  values are hard-coded into the emitted byte sequence by
  `sljit_emit_op_custom`.

## 5. Sweep axes and benchmark options

### 5.1 Axes

```cpp
SweepAxes axes() const override {
  return {
      Axis::geom_range("branches",    1, 1 << 9,  /*k=*/1),   // 1..512
      Axis::geom_range("history_len", 4, 1 << 12, /*k=*/1),   // 4..4096
  };
}
```

Default Cartesian = 10 × 11 = 110 points. Typical run time ≈ 4 min
with the standard 10-rep schedule, comparable to other ferret
benchmarks. User widens with `--branches=1..2048 --history_len=2..65536`
or densifies with `@k` suffix.

The first axis `branches` varies slowest in the CSV (per ferret's
column-order convention).

### 5.2 Per-bench options

```cpp
BenchOptions options() const override {
  return {
      BenchOption{.name = "pattern",       .default_value = 1 /* random */},
      BenchOption{.name = "spacing_bytes", .default_value = 16},
  };
}
```

- `--pattern=0` (`zero`): all-not-taken trivial baseline. The
  resulting curve is flat at the front-end's branch-per-cycle limit
  across both axes — a useful control curve confirming the kernel
  itself isn't pathological.
- `--pattern=1` (`random`): per-entry random `{0,1}`, seeded by
  `--seed` (existing global option) mixed with `(branches,
  history_len)` so distinct grid points get distinct fills.
- `--spacing_bytes=N`: per-site PC stride. Validated against
  `kMinSiteBytes` and `kBranchAlign` as in §4.2.

`pattern` is exposed as an int rather than a string because ferret's
`BenchOption` carries an int64 default. The CLI accepts `0`/`1`; the
docs page documents which is which.

### 5.3 `sites_per_kernel` and `iterations`

```cpp
size_t sites_per_kernel(const Params& p) const override {
  return p.get<size_t>("branches");
}
size_t iterations(const Params& p) const override {
  // Target ~10M sites per emit; match direct_branch_footprint's shape.
  return std::max<size_t>(1, 10'000'000 / p.get<size_t>("branches"));
}
```

Per-site cost = ticks / (iterations × branches). The runner converts
to cycles when `--freq` is set, nanoseconds otherwise.

## 6. Class shape

```cpp
struct BranchHistoryFootprint : Benchmark {
  // Owned per-instance state, lives across emit_kernel → verify_layout
  // → runner.measure for one (branches, history_len) point.
  // flat_ is logically flat[history_len][branches] in row-major order
  // (see §4.1) — consecutive sites in one outer iter read consecutive
  // words.
  std::vector<uint32_t> flat_;
  std::vector<sljit_label*> last_labels_;
  size_t last_branches_ = 0;
  size_t last_spacing_ = 0;

  std::string name() const override { return "branch_history_footprint"; }
  SweepAxes  axes()    const override;   // §5.1
  BenchOptions options() const override; // §5.2
  size_t sites_per_kernel(const Params&) const override;
  size_t iterations(const Params&) const override;
  void emit_kernel(sljit_compiler*, const Params&) override;
  void verify_layout(sljit_compiler*) override;
};

FERRET_BENCHMARK("branch_history_footprint", BranchHistoryFootprint);
```

`emit_kernel` is responsible for: validating `spacing_bytes`,
resizing+filling `flat_` for the current `(branches, history_len)`,
then emitting the kernel with `flat_.data()`, `branches`,
`history_len` baked as `SLJIT_IMM`s.

## 7. Runtime data plumbing

This is the only structural novelty vs. existing benchmarks.

### 7.1 Allocation

`flat_` is sized to `branches * history_len * sizeof(uint32_t)` and
indexed as `flat_[hist_idx * branches + j]` — transposed layout, see
§4.1. It's resized at the top of every `emit_kernel`, which may
reallocate and move the underlying storage. The kernel for the
*current* point is emitted *after* this resize, so the baked
`flat_base` immediate always points at live storage. The previous
emission is dropped before the next because `JittedKernel`'s
destructor releases the executable mapping when the runner moves to
the next point.

### 7.2 Random fill

```cpp
// Seed mix matches direct_branch_footprint's convention so seeds
// stay reproducible across benchmarks.
uint64_t mixed = static_cast<uint64_t>(seed)
               ^ (static_cast<uint64_t>(branches)    * 0x9E3779B97F4A7C15ULL)
               ^ (static_cast<uint64_t>(history_len) * 0xBF58476D1CE4E5B9ULL);

if (pattern == 0) {
  std::fill(flat_.begin(), flat_.end(), 0u);
} else {
  std::mt19937_64 rng(mixed);
  for (auto& v : flat_) v = rng() & 1u;
}
```

### 7.3 JIT immediates

`emit_kernel` bakes four immediates into the generated code:

1. `flat_base` — pointer to `flat_.data()`, loaded into a callee-saved
   register at function entry and used as the row-pointer base.
2. `branches * 4` — the row stride, used in the per-iteration MADD/LEA
   computing `row_ptr = flat_base + hist_idx * (branches * 4)`.
3. `history_len` — compared against the history index at the end of
   each iteration to wrap.
4. `iters` — outer-loop counter initial value.

Additionally, each branch site bakes a per-branch displacement `j*4`
into its load encoding. These are not sljit `SLJIT_IMM`s — they're
literal bytes inside the hand-emitted load opcode.

All four runtime immediates are 64-bit on 64-bit ISAs; sljit's
`SLJIT_IMM` argument is `sljit_sw`, which is wide enough.

### 7.4 Lifetime

The benchmark instance is created by the registry factory once per
`ferret run` invocation and lives until the run completes. Members
survive across all `(branches, history_len)` points in a single
sweep. There is no concurrency concern because ferret is single-
threaded inside `runner::measure`.

## 8. CSV impact

`branch_history_footprint` adds two axis columns (`branches`,
`history_len`) and two option columns (`pattern`, `spacing_bytes`) to
the right of `benchmark`. The schema is the same as every other
benchmark — no plot script or downstream tooling changes.

Existing column conventions per `direct_branch_footprint`:

```
benchmark,branches,history_len,pattern,spacing_bytes,ticks_min,ticks_median,iterations,sites_per_kernel,cycles_per_site,ns_per_site
branch_history_footprint,1,4,1,16,...
branch_history_footprint,1,8,1,16,...
...
```

`scripts/plot.py heatmap` already supports 2-axis CSVs (the
`direct_branch_footprint` `branches × spacing_bytes` heatmap is the
prior art). No script changes.

## 9. Error handling

Pre-codegen validation in `emit_kernel`, thrown as
`std::invalid_argument` *before* any sljit calls so the compiler
state stays clean (same discipline as `direct_branch_footprint`):

- `spacing_bytes < kMinSiteBytes` → "spacing_bytes=X is smaller than
  the site encoding (Y bytes) on this architecture" (Y = 8 on
  AArch64, 9 on x86_64).
- `spacing_bytes % kBranchAlign != 0` → "spacing_bytes=X must be a
  multiple of Y on this architecture".
- `history_len < 1` → "history_len must be >= 1".
- `branches < 1` → "branches must be >= 1".
- `branches > 4095` on AArch64 → "branches=X exceeds the unsigned
  12-bit-scaled ldr immediate limit (4095) on this architecture".
  x86_64 has no equivalent limit (disp32 covers the realistic range).
- `pattern not in {0, 1}` → "pattern must be 0 (zero) or 1 (random)".

Post-codegen verification in `verify_layout`:

- Each site offset from `labels[0]` equals `i * spacing_bytes`.
  Mismatch throws `std::runtime_error` with the per-site delta, same
  shape as `direct_branch_footprint::verify_layout`.

Allocation failure (`flat_.resize(...)` throws `std::bad_alloc`) is
propagated; the runner already reports the param point and exits with
non-zero status, no extra handling needed here.

## 10. Testing

Mirrors the `direct_branch_footprint` / `nested_call_depth` test
suite layout under `tests/`.

### 10.1 Unit tests (`tests/test_branch_history_footprint.cpp`)

- **Registry**: `branch_history_footprint` resolves via
  `BenchmarkRegistry::create`.
- **Axes**: `axes()` returns two `geom_range` axes with the expected
  names and `(lo, hi, k)`.
- **Options**: `options()` returns `pattern` and `spacing_bytes` with
  documented defaults.
- **Validation**: invalid `spacing_bytes` (too small / mis-aligned) and
  invalid `pattern` throw `std::invalid_argument` with the documented
  message prefixes.
- **Determinism of fill**: same `--seed` + same `(branches, history_-
  len, pattern=1)` produces identical `flat_` contents. The test reads
  `flat_` directly via a `friend`-declared test helper or a
  `flat_view()` `[[nodiscard]] const&` accessor added for testing —
  preference is the accessor (no friend coupling).
- **Site layout**: emit at `(branches=4, history_len=4, spacing=16)`,
  generate code, assert `verify_layout` accepts and that
  `last_labels_[i+1] - last_labels_[i] == 16` for each i. Also assert
  the disassembled load at site `i` references the per-branch
  displacement `i * 4` (via a byte-pattern check against the
  hand-emitted opcode).

### 10.2 Integration test (`tests/test_integration.cpp` extension)

Smoke-run with very small axes (`branches=1..4`, `history_len=4..8`)
and assert:

- CSV has 3 × 2 = 6 rows (`branches ∈ {1,2,4}` × `history_len ∈ {4,8}`)
  with the expected columns.
- All `ticks_min > 0`.
- The `pattern=0` baseline curve is monotonically near-flat vs.
  `branches` and `history_len`, sanity-checking the kernel itself
  isn't pathological.

### 10.3 Sanitizer matrix

ASAN, UBSAN, TSAN clean — same matrix as other benchmarks. The new
runtime-data allocation path is the highest-value target: ASAN catches
both `flat_` sizing bugs and any pointer arithmetic mistake in the
JIT-emitted indexing.

### 10.4 Manual platform check

Before merging, run the benchmark on both supported arches (Apple M-
series for AArch64; an x86_64 Linux box) and confirm the curves
exhibit the expected plateau-then-step shape under `pattern=random`
and a flat curve under `pattern=zero`. This is qualitative, not a
CI gate, but it's the only end-to-end signal that the kernel is doing
what the spec says.

## 11. Known limits

- **Predictor noise floor.** Modern predictors (TAGE-SC-L,
  perceptron-augmented) are large and noisy. Even at `pattern=zero`
  the per-site cost varies a few percent run-to-run. The min-of-K
  reps the runner already takes mitigates this; documented as a
  known caveat on the doc page.
- **Small `history_len` is a near-flat baseline.** The default lower
  bound `history_len = 4` is short enough that every shipping
  predictor handles it trivially; the leftmost columns of the heatmap
  are intended as a sanity-check baseline, not a useful capacity
  signal.
- **Outer-loop tax.** The chain-tail `cmp + csel + subs + b.ne` adds
  ~4 cycles per outer iteration regardless of `branches`. For
  `branches >= 16` this is below 25% overhead and washes out in the
  per-site division; for `branches=1` it's the dominant cost. Doc
  page warns to read the `branches=1` column as a tax baseline, not
  as "one-branch-predictor cost."
- **Branch-to-next is not the full picture.** It isolates the
  direction predictor but understates real-world mispredict cost,
  which includes redirect latency to a non-trivial target. For
  "realistic" recovery cost, see the Future Work note below.
- **Apple Silicon pinning.** Inherits the per-cluster-not-per-core
  caveat from the project README.

## 12. Future work (out of v1)

- `spacing_bytes` as a sweep axis for an aliasing study (e.g.,
  16/32/64/128 stride at fixed branches, history_len).
- `pattern=bernoulli:p` and `pattern=cycle:k` for testing predictor
  bias-handling and pattern-compression behavior.
- A companion `branch_history_recovery` benchmark using the forward-
  skip site shape (option B from brainstorming) to measure realistic
  mispredict recovery cost.
- Indirect-branch capacity benchmark (`indirect_branch_footprint`?)
  to probe the ITA/BTB-indirect structure separately from this PHT-
  focused test.
- Generalized "kernel-takes-arguments" extension to ferret's
  `Benchmark` base class once a second benchmark needs runtime data.

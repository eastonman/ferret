# `nested_call_depth` — Microbenchmark Design

Date: 2026-05-12
Status: brainstorming output, pending user review before plan generation.
Companion: [2026-05-12-ras-depth-kernel-pattern.md](2026-05-12-ras-depth-kernel-pattern.md)
captures the kernel-construction rationale and is the source of truth
for the dispatch mechanism.

## 1. Goal

`nested_call_depth` is the third ferret microbenchmark. It emits a chain
of N nested call/ret pairs and sweeps N, so the user can plot per-call
cost against depth and read off the **Return Address Stack (RAS)
capacity** as the cliff position. Per the naming convention in
`2026-05-09-ferret-design.md` §5.1, the benchmark is named after the
**workload pattern it emits** ("nested calls, swept by depth"), not the
structure it reveals.

## 2. Scope

### In scope

- Single sweep axis: `depth` (chain depth N).
- Static call graph constructed at JIT time using sljit, with
  shared-body / K=8-call-site dispatch (see §4). Memory-array path
  source (see §5).
- One configurable per-benchmark option exposed via the framework:
  `path_table_rows`.
- x86_64 and AArch64, the platforms ferret already supports.
- Unit + integration tests, same pattern as `direct_branch_footprint`.

### Out of scope

- Cross-arch behaviors beyond the existing x86_64 / AArch64 support.
- Alternative path-bit sources (LFSR, raw counter). The kernel-pattern
  doc captures these as deferred fallbacks; we adopt them only if real
  measurements show the memory-array source is being masked.
- A second sweep axis for body spacing. Body PCs are tiny (a few
  instructions each) and total static code for N ≤ 64 is well under
  any shipping I-cache or BTB-direct capacity, so spacing wouldn't
  diagnose anything for this benchmark.
- Plot adjustments. The existing `scripts/plot.py` already handles
  single-axis CSVs and will work unchanged.

## 3. Workflow

Same two-step pattern as the other benchmarks:

```sh
# Step 1: probe core frequency (existing benchmark, unchanged).
build/ferret run dependent_chain_throughput --core=3 --out=/tmp/freq.csv
python3 scripts/freq.py /tmp/freq.csv
# → estimated_freq=4.521GHz

# Step 2: sweep nesting depth, pinned to the same core, with --freq.
build/ferret run nested_call_depth --core=3 \
    --depth=1..64 --freq=4.521GHz --out=/tmp/ras.csv

# Step 3: plot cycles/call vs depth.
python3 scripts/plot.py /tmp/ras.csv --out=/tmp/ras.png
```

The cliff appears at N = RAS capacity. Below the cliff, per-call cost
is the flat baseline (call + ret + dispatch). Above the cliff, the
oldest RAS entries are evicted, those rets fall back to the BTB-indirect
predictor — which is deliberately defeated by the construction — and
per-call cost steps up by the ret-mispredict penalty.

## 4. Kernel construction

The dispatch shape and the rationale for it live in
[2026-05-12-ras-depth-kernel-pattern.md](2026-05-12-ras-depth-kernel-pattern.md).
This section locks in the v1 values; refer to the pattern doc for the
"why".

### 4.1 Static call graph

- One body per nesting level: `BODY_0` (leaf, `ret` only), `BODY_1`,
  …, `BODY_N`. Each body has **K = 8 distinct call sites**, all
  targeting the same next-level body. Their post-call PCs are the
  K possible return targets for the callee's single `ret`.
- `chain_main` mirrors the same K = 8 dispatch into `BODY_N`, so the
  **outermost** ret — the one whose RAS push is evicted first when
  capacity is exceeded — also has K distinct correct return targets.
- Per chain pass: N + 1 call/ret pairs. Runtime is linear in N.

### 4.2 K = 8 dispatch (binary-tree of conditional branches)

Each body loads one byte from the path table (see §5) and uses a
binary tree of three conditional branches to pick one of eight static
call sites. All eight call sites target the same callee PC, so
BTB-direct on the call has one target and predicts trivially. The
**return target** is what varies: BTB-indirect, which stores at most
one target per ret PC, can predict at best 1 / 8 ≈ 12.5 % of rets
after warmup. RAS, by contrast, predicts every ret correctly while
the chain depth fits in RAS.

```text
BODY_i:
    movzx  disp, byte [row_ptr + i]    ; disp ∈ [0, 8)
    test   disp, 4                      ; binary-tree dispatch
    jnz    .upper
.lower:
    test   disp, 2
    jnz    .lower_hi
.lower_lo:
    test   disp, 1
    jnz    .site_1
.site_0:
    call   BODY_{i-1} ; jmp .done
.site_1:
    call   BODY_{i-1} ; jmp .done
.lower_hi:
    test   disp, 1
    ; … sites 2, 3
.upper:
    test   disp, 2
    ; … sites 4, 5, 6, 7
.done:
    ret
```

The dispatch CBs use three direct conditional branches — no indirect
jump, so the dispatch itself does not pollute the BTB-indirect
predictor we are trying to expose.

### 4.3 Path table (the per-iteration dispatch source)

At JIT time the benchmark allocates `path_table[ROWS][N+1]`, where
`ROWS` is a power of two (see §5) and each byte is a uniform random
value in `[0, 8)` drawn from a seeded PRNG (seed mixed with `depth`
so distinct sweep points use distinct tables). The outer loop computes
`row_ptr = &path_table[iter & (ROWS - 1)][0]` once per outer iteration
and threads it through the chain in a callee-preserved register.

Each body indexes its own byte at `[row_ptr + i]`. `chain_main` uses
index 0; `BODY_N` uses index 1; …; `BODY_1` uses index N. (`BODY_0`
is a bare ret and does not dispatch.)

### 4.4 Why this defeats BTB-indirect at every depth

Each ret PC has K = 8 distinct correct return targets, drawn uniformly
across outer-loop iterations from a table whose period (`ROWS`,
default 256 — see §5) far exceeds the history any shipping indirect
predictor can fit. So:

- A simple last-target-per-PC indirect predictor mispredicts ~7/8 of
  the time on these rets.
- A history-rich indirect predictor (TAGE-indirect, ITTAGE) would
  need to memorize a per-PC table the size of `ROWS × N+1` to
  predict perfectly, which is far beyond any shipping table size.

When N ≤ RAS capacity, every ret pops the correct entry from RAS and
all of the above is academic. When N > RAS capacity, the oldest RAS
entries (corresponding to the outermost rets) are evicted, those rets
fall back to BTB-indirect, and the construction ensures they
mispredict. The cliff appears.

### 4.5 Confounders this construction does NOT isolate

- N + 1 distinct body PCs and 8(N + 1) distinct call-site PCs grow
  with N. For N ≤ 64 the total static code is a few KB — well within
  L1I and BTB-direct on every shipping core, so these are negligible.
- **Path-table cache pressure** (the one real confounder). The table
  is `ROWS × (N+1)` bytes. Each outer iter reads one row's worth, then
  moves to the next row, so over the course of a measurement we cycle
  through the entire table linearly. Once total size exceeds L1D, each
  iter incurs L1D miss latency on the row load — and that latency
  scales with depth because the rows themselves grow with N. Empirical
  result on Apple Silicon: with ROWS = 4096, pre-cliff per-call cost
  rises from ~8 cycles at N=4 to ~28 cycles at N=58 *purely from
  cache pressure*, masking the genuine RAS cliff. With ROWS = 256 (≤
  16 KB at N=64, fits L1D on every shipping core) the pre-cliff curve
  is genuinely flat. The default is set at the smallest value that
  still gives enough dispatch-pattern period to defeat history-based
  indirect predictors.

If real measurements ever show artifacts attributable to body PC
layout, we can add a body-spacing axis in a follow-up, mirroring
`direct_branch_footprint`.

## 5. Sweep axis and benchmark option

### 5.1 Axis

| Axis | Type | Range | Notes |
|------|------|-------|-------|
| `depth` | `Axis::range` (linear, step 1) | 1 → 64 | Single dense sweep, one point per integer N. |

Linear step-1 (not log2) because RAS capacities on shipping cores are
not always powers of two (e.g., Cortex-A78 ≈ 24, some Apple cores
≈ 12) and a dense sweep is needed to localize the cliff sharply.
Defaults to `1..64` to comfortably cover the largest known shipping
RAS (~32 on Zen4) plus headroom for the post-cliff plateau.

### 5.2 Per-benchmark options

| Option | Type | Default | Meaning |
|--------|------|---------|---------|
| `path_table_rows` | `int64_t`, power of 2 | 256 | Number of rows in the per-iter dispatch table. Larger ⇒ longer dispatch-pattern period ⇒ harder for history-based predictors to learn; also larger memory footprint. Once `rows × (depth+1)` exceeds L1D the measurement is dominated by cache pressure rather than predictor behavior (see §4.5). |

The default 256 rows × (N+1) bytes ≤ 16 KB at N=64 — comfortably
inside L1D on every shipping core, and an 8-bit dispatch-pattern
period is past the global-history depth of every indirect predictor
we expect to encounter. The option exists so the user can stress-test
the choice on cores with very deep indirect-prediction history (TAGE
> 8 bits), accepting cache pressure as a trade-off.

Pre-flight check: `path_table_rows` must be a power of two and ≥ 2.
Violations raise `std::invalid_argument` before any compiler state is
created, consistent with how `direct_branch_footprint` validates
`spacing_bytes`.

## 6. Class shape

```cpp
struct NestedCallDepth : Benchmark {
  std::string name() const override { return "nested_call_depth"; }

  SweepAxes axes() const override {
    return { Axis::range("depth", 1, 64) };
  }

  BenchOptions options() const override {
    return { BenchOption{ .name = "path_table_rows", .default_value = 256 } };
  }

  size_t sites_per_kernel(const Params& p) const override {
    // N + 1 call/ret pairs per chain pass (chain_main + BODY_N..BODY_1).
    return p.get<size_t>("depth") + 1;
  }

  size_t iterations(const Params& p) const override {
    // Target ~10 ms of kernel work; baseline per call+ret ~20 cycles
    // (call + ret + 3 CBs + load). Mirrors the budgeting logic in
    // direct_branch_footprint; the constant is tuned in the plan.
    return std::max<size_t>(1, 1'000'000 / (p.get<size_t>("depth") + 1));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override;
};

FERRET_BENCHMARK("nested_call_depth", NestedCallDepth);
```

## 7. CSV impact

No schema changes. The framework already emits one column per axis
(`depth`), one column per option (`path_table_rows`), the standard
`ticks_*`, `ns_per_site_*`, and (with `--freq`) `cycles_per_site_*`
columns. `sites_per_iter` is reported as `depth + 1` — the number of
call/ret pairs executed per chain pass (chain_main → BODY_N → … →
BODY_0 and back). The per-site cost the framework reports is therefore
the cost of one matched call/ret pair at the swept depth.

## 8. Error handling

Following the framework's three-class model from
`2026-05-09-ferret-design.md` §7:

- **Configuration errors.** `path_table_rows` not a power of two, or
  `depth < 1`. Surfaced before any sljit state exists, via
  `std::invalid_argument`. CLI11 / pre-flight prints an actionable
  message and exits non-zero. No partial CSV.
- **JIT failures.** Same as the existing benchmarks — sljit error
  per row writes an empty `ticks_*` row and continues.
- **Runtime.** `fn()` is not wrapped. A bug in the emitter that
  produces a crashing kernel will SIGSEGV the process; that is a
  benchmark bug, not a runtime to recover from.

## 9. Testing

### Unit tests

A new `tests/test_nested_call_depth.cpp`:

- Construct the benchmark from the registry, verify
  `name() == "nested_call_depth"`, axes contain one `depth` axis,
  options contain one `path_table_rows` option with default 256.
- Verify pre-flight rejects `path_table_rows = 3` (not a power of 2)
  and `depth = 0`.

### Integration test

A new entry in `tests/test_integration.cpp` (or a new file in the
same style as the existing one):

- Run `nested_call_depth --depth=1,2,4,8 --out=/tmp/ras_smoke.csv`,
  assert four rows with monotonically non-negative `ns_per_site_min`.
- Run with `--depth=64 --path_table_rows=256` and confirm the single
  resulting row has a non-empty `ns_per_site_min`.

### Manual validation (not in CI)

On a known x86 host (Skylake or Zen 3+), run with `--depth=1..64
--freq=<probed>` and visually confirm a cliff somewhere in the
documented RAS range (16–32). As with `direct_branch_footprint`, this
is the "did the framework actually measure the right thing" check —
not automatable in shared CI runners due to host-hardware variation
and VM noise.

## 10. Known limits

- The cliff height depends on the BTB-indirect mispredict penalty on
  the host CPU. Some cores have very weak indirect prediction for
  `ret` after RAS underflow (effectively no fallback) — the cliff is
  large. Others have a real fallback predictor — the cliff is smaller
  but still visible because of the K = 8 multi-target defeat.
- Apple silicon: on macOS our pinning falls back to a P-cluster QoS
  hint (existing limitation, documented in the project README). The
  benchmark inherits that caveat; the user should be aware that the
  measurement may land on any P-core.

## 11. Future work (out of v1 of this benchmark)

- Expose `path_source ∈ {table, lfsr, counter}` if real data shows
  the default needs alternatives for sanity-checking.
- Optional `body_spacing_bytes` axis if confounder isolation becomes
  necessary at very deep N or on unusual cores.
- A companion `indirect_call_depth` benchmark — same depth question
  but using indirect calls instead of direct, to characterize the
  BTB-indirect structure on its own.

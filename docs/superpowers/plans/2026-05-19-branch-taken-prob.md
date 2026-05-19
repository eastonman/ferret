# branch_history_footprint taken-probability knob — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `branch_history_footprint`'s binary `--pattern={0,1}` option with `--taken_prob_pct` in `[0, 100]` (default `50`), and rewrite the random fill as an independent Bernoulli draw per cell.

**Architecture:** Two source files change in lockstep — the test file asserts the new API (RED), the benchmark file is updated to satisfy it (GREEN), then a new empirical test covers the middle of the range. Docs follow. No changes to `Params`, the runner, the registry, or any other benchmark.

**Tech Stack:** C++ (sljit JIT benchmark), GoogleTest, CMake/ctest.

**Spec:** `docs/superpowers/specs/2026-05-19-branch-taken-prob-design.md`

---

## File Structure

- **Modify** `benchmarks/branch_history_footprint.cpp`
  - `options()` — rename `pattern` → `taken_prob_pct`, default `1` → `50`
  - `emit_kernel()` — rename local; replace `pattern ∈ {0,1}` validation with `taken_prob_pct ∈ [0,100]`; update fill call site
  - `branch_history_footprint_internal::generate_pattern_fill()` — rename third parameter; replace the `pattern == 0` early-return + `rng() & 1U` loop with a single Bernoulli-over-percent loop
- **Modify** `tests/test_branch_history_footprint.cpp`
  - Update the extern declaration for `generate_pattern_fill` (parameter rename)
  - Update `make_params` helper (rename, change default)
  - Rename / retarget existing tests; add two new endpoint tests and one empirical mid-range test
- **Modify** `docs/benchmarks/branch_history_footprint.md`
  - Per-bench options table, CLI surface table, "reading the curves" paragraph

---

## Task 1: Update tests to the new API (RED)

This task rewrites the test file in a single coherent pass so the file stays compilable as gtest source even while the implementation still uses the old name. The build of `test_branch_history_footprint` will fail at link or test-run time because the implementation still exports the old `pattern`-parameter signature and old option name.

**Files:**
- Modify: `tests/test_branch_history_footprint.cpp`

- [ ] **Step 1: Update the extern declaration and `make_params` helper**

Edit `tests/test_branch_history_footprint.cpp` lines 14-40 to:

```cpp
namespace ferret::branch_history_footprint_internal {
// Exposed for unit testing; defined in benchmarks/branch_history_footprint.cpp.
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t history_len, int64_t taken_prob_pct,
                                            uint64_t seed);
struct LayoutSnapshot {
  std::vector<sljit_label*> labels;
  size_t branches;
  size_t spacing;
};
LayoutSnapshot last_layout_snapshot();
}  // namespace ferret::branch_history_footprint_internal

TEST(BranchHistoryFootprint, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "branch_history_footprint");
}

namespace {
ferret::Params make_params(int64_t branches, int64_t history_len, int64_t taken_prob_pct = 50,
                           int64_t spacing = 16) {
  ferret::Params p;
  p.set("branches", branches);
  p.set("history_len", history_len);
  p.set("taken_prob_pct", taken_prob_pct);
  p.set("spacing_bytes", spacing);
  p.set("seed", 1);
  return p;
}
```

- [ ] **Step 2: Update the options-exposure test**

Replace the `ExposesPatternAndSpacingBytesOptions` test (around lines 99-112) with:

```cpp
TEST(BranchHistoryFootprint, ExposesTakenProbPctAndSpacingBytesOptions) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 2u);

  const auto* prob = find_option(opts, "taken_prob_pct");
  ASSERT_NE(prob, nullptr);
  EXPECT_EQ(prob->default_value, 50);  // 50/50 random — preserves old --pattern=1

  const auto* spacing = find_option(opts, "spacing_bytes");
  ASSERT_NE(spacing, nullptr);
  EXPECT_EQ(spacing->default_value, 16);
}
```

- [ ] **Step 3: Replace the validation test**

Replace `RejectsInvalidPattern` (around lines 136-141) with a two-endpoint range-rejection test:

```cpp
TEST(BranchHistoryFootprint, RejectsOutOfRangeProbability) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch_lo;
  EXPECT_THROW(b->emit_kernel(ch_lo.c, make_params(1, 4, /*taken_prob_pct=*/-1)),
               std::invalid_argument);
  CompilerHandle ch_hi;
  EXPECT_THROW(b->emit_kernel(ch_hi.c, make_params(1, 4, /*taken_prob_pct=*/101)),
               std::invalid_argument);
}
```

- [ ] **Step 4: Replace the all-zeros fill test and add an all-ones test**

Replace `ZeroPatternProducesAllZeros` (around lines 143-148) with:

```cpp
TEST(BranchHistoryFootprint, ZeroProbabilityProducesAllZeros) {
  auto v = ferret::branch_history_footprint_internal::generate_pattern_fill(
      /*branches=*/4, /*history_len=*/8, /*taken_prob_pct=*/0, /*seed=*/1);
  ASSERT_EQ(v.size(), 32u);
  for (uint32_t x : v) EXPECT_EQ(x, 0u);
}

TEST(BranchHistoryFootprint, HundredProbabilityProducesAllOnes) {
  auto v = ferret::branch_history_footprint_internal::generate_pattern_fill(
      /*branches=*/4, /*history_len=*/8, /*taken_prob_pct=*/100, /*seed=*/1);
  ASSERT_EQ(v.size(), 32u);
  for (uint32_t x : v) EXPECT_EQ(x, 1u);
}
```

- [ ] **Step 5: Retarget the remaining random-fill tests**

Update the three existing `RandomPattern*` tests (lines 150-176) and `RandomPatternValuesAreZeroOrOne` (lines 178-183) to pass `/*taken_prob_pct=*/50` instead of `1`. They keep the same name and semantics:

```cpp
TEST(BranchHistoryFootprint, RandomPatternIsDeterministicForSameSeed) {
  auto a = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 50, 42);
  auto b = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 50, 42);
  EXPECT_EQ(a, b);
}

TEST(BranchHistoryFootprint, RandomPatternDiffersBetweenSeeds) {
  auto a = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 50, 42);
  auto b = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 50, 43);
  EXPECT_NE(a, b);
}

TEST(BranchHistoryFootprint, RandomPatternDiffersByParamPoint) {
  auto a = ferret::branch_history_footprint_internal::generate_pattern_fill(4, 16, 50, 1);
  auto b = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 50, 1);
  size_t common = std::min(a.size(), b.size());
  bool any_diff = false;
  for (size_t i = 0; i < common; ++i) {
    if (a[i] != b[i]) {
      any_diff = true;
      break;
    }
  }
  EXPECT_TRUE(any_diff);
}

TEST(BranchHistoryFootprint, RandomPatternValuesAreZeroOrOne) {
  auto v = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 32, 50, 7);
  for (uint32_t x : v) {
    EXPECT_TRUE(x == 0u || x == 1u) << "value out of {0,1}: " << x;
  }
}
```

- [ ] **Step 6: Add the empirical mid-range test**

Append this new test after `RandomPatternValuesAreZeroOrOne` (around line 183):

```cpp
TEST(BranchHistoryFootprint, MidProbabilityRoughlyHalfTaken) {
  // 100 x 100 = 10 000 cells. 1 sigma ~ 0.005 at p=0.5; [0.45, 0.55] is
  // a ~10 sigma window — a flat-out broken Bernoulli is what this
  // catches, not statistical edges.
  auto v = ferret::branch_history_footprint_internal::generate_pattern_fill(
      /*branches=*/100, /*history_len=*/100, /*taken_prob_pct=*/50, /*seed=*/12345);
  ASSERT_EQ(v.size(), 10'000u);
  size_t taken = 0;
  for (uint32_t x : v) taken += x;
  double rate = static_cast<double>(taken) / static_cast<double>(v.size());
  EXPECT_GE(rate, 0.45) << "empirical taken-rate too low: " << rate;
  EXPECT_LE(rate, 0.55) << "empirical taken-rate too high: " << rate;
}
```

- [ ] **Step 7: Build and verify the test target fails to build (or fails at runtime) for the right reason**

Run: `cmake --build build --target test_branch_history_footprint 2>&1 | tail -40`

Expected: build fails. The most likely failure is a linker error (undefined reference to `generate_pattern_fill(size_t, size_t, int64_t, uint64_t)` with the new param-name mangling — but C++ doesn't mangle parameter names, so the link will actually succeed; instead the failure will be at *runtime* when `ExposesTakenProbPctAndSpacingBytesOptions` fails because the registered option is still named `pattern`).

If the build succeeds, run the test binary and confirm the failure mode:

Run: `./build/tests/test_branch_history_footprint --gtest_filter='BranchHistoryFootprint.ExposesTakenProbPctAndSpacingBytesOptions'`

Expected: FAIL with `find_option(opts, "taken_prob_pct")` returning null (option still named `pattern`).

- [ ] **Step 8: Do not commit yet — Task 2 will commit the test+impl change together.**

---

## Task 2: Update implementation to satisfy the new API (GREEN)

**Files:**
- Modify: `benchmarks/branch_history_footprint.cpp`

- [ ] **Step 1: Rename the option in `options()`**

In `benchmarks/branch_history_footprint.cpp` around lines 98-103, change:

```cpp
[[nodiscard]] BenchOptions options() const override {
  return {
      BenchOption{.name = "pattern", .default_value = 1},
      BenchOption{.name = "spacing_bytes", .default_value = 16},
  };
}
```

to:

```cpp
[[nodiscard]] BenchOptions options() const override {
  return {
      BenchOption{.name = "taken_prob_pct", .default_value = 50},
      BenchOption{.name = "spacing_bytes", .default_value = 16},
  };
}
```

- [ ] **Step 2: Update `emit_kernel` local + validation**

In `emit_kernel` (around lines 115-138), rename the local and replace the validation block:

```cpp
auto branches = p.get<size_t>("branches");
auto history_len = p.get<size_t>("history_len");
auto taken_prob_pct = p.get<int64_t>("taken_prob_pct");
auto spacing = p.get<size_t>("spacing_bytes");
auto seed = static_cast<uint64_t>(p.get<int64_t>("seed"));

// Pre-codegen validation (must throw before mutating `c`).
// `branches` check runs before iterations() below — x86_64 traps on
// the 10_000_000 / branches divide-by-zero.
if (branches < 1) {
  throw std::invalid_argument("branches must be >= 1");
}
if (history_len < 1) {
  throw std::invalid_argument("history_len must be >= 1");
}
if (taken_prob_pct < 0 || taken_prob_pct > 100) {
  throw std::invalid_argument("taken_prob_pct must be in [0, 100]; got " +
                              std::to_string(taken_prob_pct));
}
if (spacing < kMinSiteBytes) {
  throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                              " is smaller than the minimum site encoding (" + std::to_string(kMinSiteBytes) +
                              " bytes) on this architecture");
}
```

- [ ] **Step 3: Update the fill call site in `emit_kernel`**

A few lines further down (around line 142), update the call:

```cpp
flat_ = branch_history_footprint_internal::generate_pattern_fill(branches, history_len, taken_prob_pct, seed);
```

- [ ] **Step 4: Rewrite `generate_pattern_fill`**

Replace the body of `generate_pattern_fill` (around lines 49-63) with:

```cpp
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t history_len, int64_t taken_prob_pct,
                                            uint64_t seed) {
  std::vector<uint32_t> flat(branches * history_len, 0U);
  // Mix seed with (branches, history_len) so distinct grid points get
  // distinct fills.
  uint64_t mixed = mix_seed(seed, branches, history_len);
  std::mt19937_64 rng(mixed);
  auto threshold = static_cast<uint64_t>(taken_prob_pct);
  for (auto& v : flat) {
    v = (rng() % 100ULL) < threshold ? 1U : 0U;
  }
  return flat;
}
```

Note: the `pattern == 0` early-return is gone — `taken_prob_pct == 0` falls out of the same loop (threshold is `0`, comparison is always false, all cells stay `0`). `taken_prob_pct == 100` similarly: threshold is `100`, `rng() % 100` is always `< 100`, all cells are `1`.

- [ ] **Step 5: Build the test target**

Run: `cmake --build build --target test_branch_history_footprint`

Expected: clean build with no warnings.

- [ ] **Step 6: Run the benchmark's unit tests**

Run: `ctest --test-dir build -R BranchHistoryFootprint --output-on-failure`

Expected: all tests PASS, including the three new ones (`ExposesTakenProbPctAndSpacingBytesOptions`, `RejectsOutOfRangeProbability`, `ZeroProbabilityProducesAllZeros`, `HundredProbabilityProducesAllOnes`, `MidProbabilityRoughlyHalfTaken`).

- [ ] **Step 7: Run the full test suite to confirm no regression in other benchmarks**

Run: `ctest --test-dir build --output-on-failure`

Expected: all tests PASS.

- [ ] **Step 8: Commit**

```bash
git add benchmarks/branch_history_footprint.cpp tests/test_branch_history_footprint.cpp
git commit --no-gpg-sign -m "$(cat <<'EOF'
feat(branch_history_footprint): taken-probability knob

Replace --pattern={0,1} with --taken_prob_pct in [0,100] (default 50,
preserving the old 50/50 random behavior). Random fill is independent
Bernoulli per cell via rng()%100<p; pattern=0 and pattern=100 fall out
of the same loop body with no special casing.

Unlocks --taken_prob_pct=100 (always-taken) as a second trivial-prediction
baseline alongside the existing always-not-taken (=0).
EOF
)"
```

---

## Task 3: Update the benchmark's user-facing doc

**Files:**
- Modify: `docs/benchmarks/branch_history_footprint.md`

- [ ] **Step 1: Replace the "Per-benchmark options" table**

In `docs/benchmarks/branch_history_footprint.md` around lines 56-63, replace:

```markdown
## Per-benchmark options

| flag                  | meaning                                                  |
| --------------------- | -------------------------------------------------------- |
| `--pattern=0`         | All-zero fill — all-not-taken trivial baseline.          |
| `--pattern=1` (def.)  | Per-entry random `{0,1}` seeded by `--seed`.             |
| `--spacing_bytes=16`  | Minimum PC stride per site. Min 8 (AArch64) / 6 (x86_64). |
```

with:

```markdown
## Per-benchmark options

| flag                          | meaning                                                  |
| ----------------------------- | -------------------------------------------------------- |
| `--taken_prob_pct=N` (def. 50) | Per-cell probability of *taken* in percent, `N` in `[0, 100]`. `0` = all-not-taken; `100` = all-taken; `50` = max-entropy iid 50/50 (the old `--pattern=1`). Independent Bernoulli per cell, seeded by `--seed`. |
| `--spacing_bytes=16`          | Minimum PC stride per site. Min 8 (AArch64) / 6 (x86_64). |
```

- [ ] **Step 2: Replace the row in the CLI surface table**

In the "CLI surface" table around lines 66-75, replace the `--pattern` row:

```markdown
| `--pattern=0\|1`           | See above. Default `1`.                                                |
```

with:

```markdown
| `--taken_prob_pct=N`       | See above. Integer percent in `[0, 100]`. Default `50`.                |
```

- [ ] **Step 3: Update the "Reading the curves" paragraph**

In the "Reading the curves" section around lines 94-97, replace:

```markdown
Running with `--pattern=0` gives a flat control surface across both
axes (always-not-taken is trivial to predict regardless of count).
Compare against the `--pattern=1` heatmap to confirm the cliff is
predictor-driven, not kernel-driven.
```

with:

```markdown
Running with `--taken_prob_pct=0` (always-not-taken) or `=100`
(always-taken) gives a flat control surface across both axes — both
endpoints are trivial to predict regardless of count. Compare either
against the default `=50` heatmap to confirm the cliff is
predictor-driven, not kernel-driven.

A `--taken_prob_pct` sweep at fixed `(branches, history_len)` in the
post-cliff region traces a U-shape: cost is low near `0` and `100`
(low per-branch entropy) and peaks near `50` (max entropy).
```

- [ ] **Step 4: Commit**

```bash
git add docs/benchmarks/branch_history_footprint.md
git commit --no-gpg-sign -m "$(cat <<'EOF'
docs(branch_history_footprint): document taken_prob_pct knob

Document the new --taken_prob_pct option, including both trivial-
prediction endpoints (0 and 100) and the U-shape entropy sweep at
fixed (branches, history_len).
EOF
)"
```

---

## Task 4: Manual smoke

**Files:**
- None modified — just a build + run sanity check.

- [ ] **Step 1: Confirm the option appears in the registered axes printed by the benchmark**

Run: `./build/ferret list`

Expected: `branch_history_footprint` listed.

Run: `./build/ferret run branch_history_footprint --help 2>&1 | head -20` (if `--help` is supported; otherwise skip).

- [ ] **Step 2: Run a small sweep across the probability axis**

Run:

```bash
./build/ferret run branch_history_footprint \
  --branches=64 --history_len=256 \
  --taken_prob_pct=0,25,50,75,100 \
  --out=/tmp/bhp_prob_sweep.csv
```

Expected: process exits 0; CSV is written.

- [ ] **Step 3: Eyeball the CSV**

Run: `cat /tmp/bhp_prob_sweep.csv`

Expected: 5 data rows, one per `taken_prob_pct` value. The `taken_prob_pct` column is present (replacing the old `pattern` column). The cost-per-site column should be low at `0` and `100` (trivial prediction), higher at `25`/`75`, peak near `50`. A monotone or near-monotone-then-mirror shape — don't fail the task on noise; this is a sanity eyeball, not an assertion.

- [ ] **Step 4: Verify the old option name is now an error**

Run:

```bash
./build/ferret run branch_history_footprint \
  --branches=64 --history_len=256 --pattern=1 --out=/tmp/should_fail.csv ; echo "exit=$?"
```

Expected: non-zero exit with a CLI error about unknown option `--pattern` (the ferret CLI's standard "unexpected argument" handling — see `parse_extras` in `src/cli_axis.cpp`).

- [ ] **Step 5: No-op commit point**

Nothing to commit from this task; if the manual sweep surfaces a real issue, fix it and amend the appropriate prior task's commit instead.

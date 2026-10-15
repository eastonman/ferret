# `train_betray_latency` Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `train_betray_latency` — a train-and-betray microbenchmark that reports per-branch mispredict penalty in cycles (or nanoseconds without `--freq`).

**Architecture:** Reuses the `branch_history_footprint` chain shape (data-dependent conditional branches with branch-target = next instruction). Pattern buffer is fixed: M rows of all-1 (training) plus 1 row of all-0 (betrayal). Per-row measurement does two-kernel differencing — betray kernel minus an all-train control kernel — so the residual ticks are exactly the K mispredicts. Requires a small framework refactor that moves per-row measurement responsibility from `run_command::measure_all` to a new `Benchmark::measure_row` virtual.

**Tech Stack:** C++20, sljit (JIT), CMake/Ninja, GoogleTest, ferret's existing benchmark framework.

**Reference docs:**
- Spec: [`../specs/2026-05-27-mispredict-latency-design.md`](../specs/2026-05-27-mispredict-latency-design.md)
- Sister benchmark (capacity, not latency): [`../specs/2026-05-17-branch-history-footprint-design.md`](../specs/2026-05-17-branch-history-footprint-design.md), [`benchmarks/branch_history_footprint.cpp`](../../../benchmarks/branch_history_footprint.cpp)
- Build/test recipes: [`docs/build.md`](../../build.md), [`AGENTS.md`](../../../AGENTS.md)

**Build & test cycle (used in every task):**
```sh
cmake --build build
ctest --test-dir build --output-on-failure
```
If you don't have a build dir yet:
```sh
nix develop -c cmake -S . -B build -GNinja
```

---

## File Structure

**New files:**
- `benchmarks/train_betray_latency.cpp` — benchmark class, pattern fill, kernel emit, measure_row.
- `tests/test_train_betray_latency.cpp` — unit + integration tests for the benchmark.
- `docs/benchmarks/train_betray_latency.md` — per-benchmark documentation page.

**Modified files:**
- `include/ferret/benchmark.hpp` — add pure-virtual `Benchmark::measure_row`.
- `include/ferret/runner.hpp` — declare `runner::single_kernel_measure` helper.
- `src/runner.cpp` — implement the helper (lifted from `measure_all`'s inner block).
- `src/run_command.cpp` — collapse `measure_all` to a `bench.measure_row(...)` call per row.
- `benchmarks/branch_history_footprint.cpp` — add one-line `measure_row` override.
- `benchmarks/direct_branch_footprint.cpp` — add one-line `measure_row` override.
- `benchmarks/nested_call_depth.cpp` — add one-line `measure_row` override.
- `benchmarks/dependent_chain_throughput.cpp` — add one-line `measure_row` override.
- `tests/test_runner.cpp` — add unit test for `single_kernel_measure`.
- `tests/CMakeLists.txt` — add `test_train_betray_latency` target; ensure `test_runner` links sljit + benchmark.
- `CMakeLists.txt` — add `benchmarks/train_betray_latency.cpp` to `ferret_benchmarks`.
- `README.md` — add the benchmark to the benchmark table.

---

### Task 1: Add `runner::single_kernel_measure` helper

Pull the "build one JittedKernel, time it, fill a row" logic out of `measure_all` so the new differencing benchmark can build its own measurement strategy on top of the same primitive.

**Files:**
- Modify: `include/ferret/runner.hpp`
- Modify: `src/runner.cpp`
- Modify: `tests/test_runner.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_runner.cpp`:

```cpp
#include "ferret/benchmark.hpp"
#include "ferret/jit.hpp"
#include "ferret/params.hpp"

extern "C" {
#include <sljitLir.h>
}

namespace {
struct NoopBenchmark : public ferret::Benchmark {
  std::string name() const override { return "noop"; }
  ferret::SweepAxes axes() const override { return {}; }
  size_t sites_per_kernel(const ferret::Params&) const override { return 1; }
  size_t iterations(const ferret::Params&) const override { return 1; }
  void emit_kernel(sljit_compiler* c, const ferret::Params&) override {
    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 1, 0, 0);
    sljit_emit_return_void(c);
  }
};
}  // namespace

TEST(SingleKernelMeasure, RunsNoopBenchmarkSuccessfully) {
  NoopBenchmark b;
  ferret::Params p;
  auto row = ferret::runner::single_kernel_measure(b, p, /*reps=*/3, /*warmup=*/1);
  EXPECT_FALSE(row.jit_failed);
  EXPECT_EQ(row.sites, 1u);
  EXPECT_EQ(row.iters, 1u);
  EXPECT_EQ(row.reps, 3u);
  EXPECT_GE(row.ticks_median, row.ticks_min);
}
```

Note: `NoopBenchmark` does not implement `measure_row` because that virtual doesn't exist yet (Task 2 adds it). Task 2 adds the one-line override to `NoopBenchmark` when it makes the virtual pure.

- [ ] **Step 2: Add `single_kernel_measure` declaration to `runner.hpp`**

Edit `include/ferret/runner.hpp`. Forward-declare `Benchmark` and `Params`, then add the declaration to the `runner` namespace:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>

namespace ferret {
class Benchmark;
class Params;
}  // namespace ferret

namespace ferret {

struct MeasurementRow {
  uint64_t ticks_min = 0;
  uint64_t ticks_median = 0;
  size_t iters = 0;
  size_t sites = 0;
  size_t reps = 0;
  bool jit_failed = false;
};

namespace runner {

using KernelFn = void (*)(void);

struct MeasureOptions {
  size_t iters;
  size_t sites;
  int reps;
  int warmup;
};

MeasurementRow measure(KernelFn fn, const MeasureOptions& opts);

// Builds a JittedKernel for `b` at `p`, runs runner::measure on it, and
// fills the resulting MeasurementRow with iters / sites taken from the
// benchmark. The default measure_row strategy for benchmarks that time
// a single JIT'd kernel — differencing benchmarks override measure_row
// directly and don't use this helper.
//
// On JIT failure, returns a row with .jit_failed = true and
// .iters/.sites pre-populated. Propagates std::exception from
// emit_kernel/verify_layout to the caller.
MeasurementRow single_kernel_measure(Benchmark& b, const Params& p,
                                     int reps, int warmup);

}  // namespace runner
}  // namespace ferret
```

- [ ] **Step 3: Implement `single_kernel_measure` in `runner.cpp`**

Edit `src/runner.cpp`. Add includes and the new function body. Lift the logic out of `measure_all` (don't delete the original yet — Task 2 does that):

```cpp
#include "ferret/runner.hpp"

#include "ferret/benchmark.hpp"
#include "ferret/jit.hpp"
#include "ferret/log.hpp"
#include "ferret/params.hpp"
#include "ferret/timing.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace flog = ferret::log;

namespace ferret::runner {

MeasurementRow measure(KernelFn fn, const MeasureOptions& opts) {
  if (opts.reps <= 0) {
    throw std::invalid_argument("runner::measure: reps must be >= 1");
  }
  if (opts.warmup < 0) {
    throw std::invalid_argument("runner::measure: warmup must be >= 0");
  }
  MeasurementRow row;
  row.iters = opts.iters;
  row.sites = opts.sites;
  row.reps = static_cast<size_t>(opts.reps);

  for (int i = 0; i < opts.warmup; ++i) {
    fn();
  }

  std::vector<uint64_t> samples;
  samples.reserve(opts.reps);
  for (int i = 0; i < opts.reps; ++i) {
    uint64_t t0 = timing::arch_now_ticks();
    fn();
    uint64_t t1 = timing::arch_now_ticks();
    samples.push_back(t1 - t0);
  }

  std::ranges::sort(samples);
  row.ticks_min = samples.front();
  row.ticks_median = samples[samples.size() / 2];
  return row;
}

MeasurementRow single_kernel_measure(Benchmark& b, const Params& p,
                                     int reps, int warmup) {
  size_t pre_iters = b.iterations(p);
  size_t pre_sites = b.sites_per_kernel(p);
  if (pre_iters == 0 || pre_sites == 0) {
    throw std::invalid_argument("single_kernel_measure: iterations and sites_per_kernel must be > 0");
  }
  JittedKernel kern(b, p);
  if (!kern.ok()) {
    MeasurementRow row;
    row.jit_failed = true;
    row.iters = pre_iters;
    row.sites = pre_sites;
    flog::warn("sljit_error on params; emitting empty row");
    return row;
  }
  flog::info("jit kernel: {} bytes", kern.code_size());
  return measure(kern.fn(), {.iters = pre_iters, .sites = pre_sites, .reps = reps, .warmup = warmup});
}

}  // namespace ferret::runner
```

- [ ] **Step 4: Update `tests/CMakeLists.txt` so `test_runner` can link sljit and the benchmark machinery**

Edit `tests/CMakeLists.txt`. Replace the `test_runner` target block:

```cmake
add_executable(test_runner test_runner.cpp)
target_link_libraries(test_runner PRIVATE
  ferret_core
  sljit::sljit
  GTest::gtest GTest::gtest_main
  ferret_warnings
)
gtest_discover_tests(test_runner)
```

(The only change is the addition of `sljit::sljit`.)

- [ ] **Step 5: Run the new test**

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R SingleKernelMeasure
```
Expected: PASS — `single_kernel_measure` is declared, defined, and exercises the JIT/measure pipeline against a noop benchmark.

- [ ] **Step 6: Run the full suite to confirm no regressions**

```sh
ctest --test-dir build --output-on-failure
```
Expected: PASS. `measure_all` in `src/run_command.cpp` is unchanged at this point — it still constructs `JittedKernel` and calls `runner::measure` directly. The new helper duplicates that logic; Task 2 deletes the duplicate.

- [ ] **Step 7: Commit**

```sh
git add include/ferret/runner.hpp src/runner.cpp \
        tests/test_runner.cpp tests/CMakeLists.txt
git commit --no-gpg-sign -m "feat(runner): add single_kernel_measure helper"
```

---

### Task 2: Introduce `Benchmark::measure_row`, port all benchmarks, simplify `measure_all`

Add the pure-virtual `measure_row` contract to `Benchmark`. Every existing benchmark gets a one-line override that delegates to `runner::single_kernel_measure`. `run_command::measure_all` collapses to a `bench.measure_row(p, reps, warmup)` call per row.

**Files:**
- Modify: `include/ferret/benchmark.hpp`
- Modify: `benchmarks/branch_history_footprint.cpp`
- Modify: `benchmarks/direct_branch_footprint.cpp`
- Modify: `benchmarks/nested_call_depth.cpp`
- Modify: `benchmarks/dependent_chain_throughput.cpp`
- Modify: `tests/test_runner.cpp` (NoopBenchmark needs to implement the new pure-virtual)
- Modify: `src/run_command.cpp`

- [ ] **Step 1: Add `measure_row` pure-virtual to `Benchmark`**

Edit `include/ferret/benchmark.hpp`. Add `runner.hpp` include for `MeasurementRow`, and add the pure-virtual to the class:

```cpp
#include "ferret/runner.hpp"

namespace ferret {

class Benchmark {
 public:
  virtual ~Benchmark() = default;
  virtual std::string name() const = 0;
  virtual SweepAxes axes() const = 0;
  virtual BenchOptions options() const { return {}; }
  virtual size_t sites_per_kernel(const Params& p) const = 0;
  virtual size_t iterations(const Params& p) const = 0;
  virtual void emit_kernel(sljit_compiler* c, const Params& p) = 0;
  virtual void verify_layout(sljit_compiler* c) { (void)c; }

  // Per-row measurement strategy. Most benchmarks build one JIT'd
  // kernel and time it; they implement this as a one-liner that calls
  // runner::single_kernel_measure(*this, p, reps, warmup). Benchmarks
  // that need a richer strategy (e.g. train_betray_latency, which times
  // two kernels and reports the difference) override directly.
  virtual MeasurementRow measure_row(const Params& p, int reps, int warmup) = 0;
};
```

- [ ] **Step 2: Add the one-liner override to each existing benchmark**

For each of:
- `benchmarks/branch_history_footprint.cpp`
- `benchmarks/direct_branch_footprint.cpp`
- `benchmarks/nested_call_depth.cpp`
- `benchmarks/dependent_chain_throughput.cpp`

Add the include and the override. For `branch_history_footprint.cpp`, the include is already present indirectly — add `#include "ferret/runner.hpp"` to the includes block, then add this method to the `BranchHistoryFootprint` struct (placement: right after `verify_layout`'s declaration):

```cpp
  MeasurementRow measure_row(const Params& p, int reps, int warmup) override {
    return runner::single_kernel_measure(*this, p, reps, warmup);
  }
```

For each of the other three benchmarks, do the analogous edit — add `#include "ferret/runner.hpp"` to the includes block, then add the one-liner override to the struct definition.

- [ ] **Step 2b: Add the same override to `NoopBenchmark` in `tests/test_runner.cpp`**

Edit `tests/test_runner.cpp`. Add the override to the `NoopBenchmark` struct from Task 1:

```cpp
ferret::MeasurementRow measure_row(const ferret::Params& p, int reps, int warmup) override {
  return ferret::runner::single_kernel_measure(*this, p, reps, warmup);
}
```

Without this, the test target fails to link because `NoopBenchmark` becomes abstract (cannot be instantiated) once `measure_row` is pure-virtual.

- [ ] **Step 3: Simplify `measure_all` in `src/run_command.cpp`**

Replace the body of `measure_all` (lines ~121-151) with:

```cpp
std::optional<std::vector<MeasuredRow>> measure_all(Benchmark& bench, const std::vector<Params>& rows, int reps,
                                                    int warmup) {
  std::vector<MeasuredRow> out;
  out.reserve(rows.size());
  for (const auto& p : rows) {
    MeasurementRow m;
    try {
      m = bench.measure_row(p, reps, warmup);
    } catch (const std::exception& e) {
      flog::error("benchmark error on params: {}", e.what());
      return std::nullopt;
    }
    out.push_back({p, m});
  }
  return out;
}
```

Remove the now-unused `#include "ferret/jit.hpp"` from `src/run_command.cpp` if and only if no other code in the file references `JittedKernel` (search the file for `JittedKernel` first).

- [ ] **Step 4: Build everything**

```sh
cmake --build build
```
Expected: PASS — Task 1's test now compiles (the override on `NoopBenchmark` is satisfied because `Benchmark::measure_row` exists), all 4 production benchmarks compile (each has a one-liner override), `measure_all` compiles against the new contract.

- [ ] **Step 5: Run all tests**

```sh
ctest --test-dir build --output-on-failure
```
Expected: PASS — `SingleKernelMeasure.RunsNoopBenchmarkSuccessfully` and all 19 existing test binaries pass. Behaviour is unchanged for every existing benchmark — `measure_all` now goes through one extra virtual call per row, which delegates to the same JIT+measure path it executed before.

- [ ] **Step 6: Commit**

```sh
git add include/ferret/benchmark.hpp \
        benchmarks/branch_history_footprint.cpp \
        benchmarks/direct_branch_footprint.cpp \
        benchmarks/nested_call_depth.cpp \
        benchmarks/dependent_chain_throughput.cpp \
        tests/test_runner.cpp \
        src/run_command.cpp
git commit --no-gpg-sign -m "refactor(framework): route per-row measurement through Benchmark::measure_row"
```

---

### Task 3: Skeleton `train_betray_latency` benchmark (registry, axes, options, stubs)

Get the new benchmark wired into the build and registry. `emit_kernel` and `measure_row` throw "not implemented" — implementation lands in later tasks.

**Files:**
- Create: `benchmarks/train_betray_latency.cpp`
- Modify: `CMakeLists.txt`
- Create: `tests/test_train_betray_latency.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_train_betray_latency.cpp`:

```cpp
#include <gtest/gtest.h>

#include "ferret/benchmark.hpp"
#include "ferret/params.hpp"
#include "sljit_test_helpers.hpp"

using ferret::testing::find_option;

namespace {
ferret::Params make_params(int64_t branches, int64_t train_iters = 32, int64_t spacing = 16) {
  ferret::Params p;
  p.set("branches", branches);
  p.set("train_iters", train_iters);
  p.set("spacing_bytes", spacing);
  p.set("seed", 1);
  return p;
}
}  // namespace

TEST(TrainBetrayLatency, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "train_betray_latency");
}

TEST(TrainBetrayLatency, ExposesBranchesAxis) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  ASSERT_EQ(axes.size(), 1u);
  EXPECT_EQ(axes[0].name(), "branches");
}

TEST(TrainBetrayLatency, BranchesAxisDefaultRangeMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[0].expand();
  // 256..1024 with k=1: {256, 512, 1024} = 3 points.
  EXPECT_EQ(vs.size(), 3u);
  EXPECT_EQ(vs.front(), 256);
  EXPECT_EQ(vs.back(), 1024);
}

TEST(TrainBetrayLatency, ExposesTrainItersAndSpacingBytesOptions) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 2u);

  const auto* train = find_option(opts, "train_iters");
  ASSERT_NE(train, nullptr);
  EXPECT_EQ(train->default_value, 32);

  const auto* spacing = find_option(opts, "spacing_bytes");
  ASSERT_NE(spacing, nullptr);
  EXPECT_EQ(spacing->default_value, 16);
}

TEST(TrainBetrayLatency, SitesPerKernelEqualsBranches) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->sites_per_kernel(make_params(256)), 256u);
  EXPECT_EQ(b->sites_per_kernel(make_params(1024)), 1024u);
}

TEST(TrainBetrayLatency, IterationsAmortizesAtTenMillionSites) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->iterations(make_params(256)), 10'000'000u / 256u);
  EXPECT_EQ(b->iterations(make_params(1024)), 10'000'000u / 1024u);
}
```

- [ ] **Step 2: Run the test to verify it fails**

First add the test target so it builds. Edit `tests/CMakeLists.txt`, appending:

```cmake
add_executable(test_train_betray_latency
  test_train_betray_latency.cpp
)
target_sources(test_train_betray_latency PRIVATE $<TARGET_OBJECTS:ferret_benchmarks>)
target_link_libraries(test_train_betray_latency PRIVATE
  ferret_core
  ferret_benchmarks
  sljit::sljit
  GTest::gtest GTest::gtest_main
  ferret_warnings
)
gtest_discover_tests(test_train_betray_latency)
```

Then build:
```sh
cmake -S . -B build -GNinja
cmake --build build
```
Expected: build failure because `benchmarks/train_betray_latency.cpp` does not yet exist (CMake will refuse to construct the `ferret_benchmarks` object library once we add it in the next step). Don't add it to CMakeLists yet — instead, create the source file in Step 3 then update CMakeLists in Step 4 so the build moves from "missing source" to "missing registry entry" to "passing".

- [ ] **Step 3: Create the skeleton benchmark source**

Create `benchmarks/train_betray_latency.cpp`:

```cpp
extern "C" {
#include <sljitLir.h>
}

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "ferret/bench_helpers.hpp"
#include "ferret/benchmark.hpp"
#include "ferret/runner.hpp"

namespace ferret {

namespace {
constexpr size_t kOpBudget = 10'000'000;
}  // namespace

struct TrainBetrayLatency : Benchmark {
  [[nodiscard]] std::string name() const override { return "train_betray_latency"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {Axis::geom_range("branches", 256, 1024, /*samples_per_octave=*/1)};
  }

  [[nodiscard]] BenchOptions options() const override {
    return {
        BenchOption{.name = "train_iters", .default_value = 32},
        BenchOption{.name = "spacing_bytes", .default_value = 16},
    };
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override {
    return p.get<size_t>("branches");
  }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return compute_iterations(kOpBudget, p.get<size_t>("branches"));
  }

  void emit_kernel(sljit_compiler* /*c*/, const Params& /*p*/) override {
    throw std::runtime_error("train_betray_latency::emit_kernel not yet implemented");
  }

  MeasurementRow measure_row(const Params& /*p*/, int /*reps*/, int /*warmup*/) override {
    throw std::runtime_error("train_betray_latency::measure_row not yet implemented");
  }
};

FERRET_BENCHMARK("train_betray_latency", TrainBetrayLatency);

}  // namespace ferret
```

- [ ] **Step 4: Register the new TU in CMake**

Edit `CMakeLists.txt`. Find the `ferret_benchmarks` `add_library` block and add the new source:

```cmake
add_library(ferret_benchmarks OBJECT
  benchmarks/branch_history_footprint.cpp
  benchmarks/direct_branch_footprint.cpp
  benchmarks/nested_call_depth.cpp
  benchmarks/dependent_chain_throughput.cpp
  benchmarks/train_betray_latency.cpp
)
```

- [ ] **Step 5: Build and run the test**

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R TrainBetrayLatency
```
Expected: PASS for all 6 tests in `test_train_betray_latency` (registry, axes, axis default range, options, sites_per_kernel, iterations).

- [ ] **Step 6: Run the full suite to confirm no regressions**

```sh
ctest --test-dir build --output-on-failure
```
Expected: PASS.

- [ ] **Step 7: Commit**

```sh
git add benchmarks/train_betray_latency.cpp \
        tests/test_train_betray_latency.cpp \
        tests/CMakeLists.txt CMakeLists.txt
git commit --no-gpg-sign -m "feat(train_betray_latency): scaffold benchmark with axes/options stubs"
```

---

### Task 4: Pattern fill function + `FillMode` enum

The fill produces `flat[(M+1) * K]` (`uint32_t` per slot). Two modes: `Betray` (M rows of 1, then row M of 0) and `Control` (all M+1 rows of 1). Pure function — no JIT, no state.

**Files:**
- Modify: `benchmarks/train_betray_latency.cpp`
- Modify: `tests/test_train_betray_latency.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_train_betray_latency.cpp`:

```cpp
namespace ferret::train_betray_latency_internal {
enum class FillMode { Betray, Control };
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t train_iters, FillMode mode);
}  // namespace ferret::train_betray_latency_internal

TEST(TrainBetrayLatency, BetrayFillTrainRowsAreOnesBetrayRowIsZeros) {
  using namespace ferret::train_betray_latency_internal;
  const size_t K = 4;
  const size_t M = 3;
  auto v = generate_pattern_fill(K, M, FillMode::Betray);
  ASSERT_EQ(v.size(), K * (M + 1));
  for (size_t row = 0; row < M; ++row) {
    for (size_t j = 0; j < K; ++j) {
      EXPECT_EQ(v[row * K + j], 1u) << "training row " << row << " col " << j;
    }
  }
  for (size_t j = 0; j < K; ++j) {
    EXPECT_EQ(v[M * K + j], 0u) << "betrayal row col " << j;
  }
}

TEST(TrainBetrayLatency, ControlFillAllRowsAreOnes) {
  using namespace ferret::train_betray_latency_internal;
  const size_t K = 4;
  const size_t M = 3;
  auto v = generate_pattern_fill(K, M, FillMode::Control);
  ASSERT_EQ(v.size(), K * (M + 1));
  for (uint32_t x : v) EXPECT_EQ(x, 1u);
}

TEST(TrainBetrayLatency, FillHandlesTrainItersZero) {
  using namespace ferret::train_betray_latency_internal;
  // M=0: Betray collapses to a single all-0 row; Control to a single all-1 row.
  auto betray = generate_pattern_fill(4, 0, FillMode::Betray);
  ASSERT_EQ(betray.size(), 4u);
  for (uint32_t x : betray) EXPECT_EQ(x, 0u);
  auto control = generate_pattern_fill(4, 0, FillMode::Control);
  ASSERT_EQ(control.size(), 4u);
  for (uint32_t x : control) EXPECT_EQ(x, 1u);
}
```

Make sure `#include <vector>` is at the top of the test file (add it if missing).

- [ ] **Step 2: Run tests to confirm they fail**

```sh
cmake --build build
```
Expected: build failure — undefined reference to `generate_pattern_fill`.

- [ ] **Step 3: Add `FillMode` and `generate_pattern_fill` to the benchmark TU**

Edit `benchmarks/train_betray_latency.cpp`. Add `<vector>` to includes. After the anonymous-namespace `kOpBudget` definition and before the `TrainBetrayLatency` struct, add the namespace + helper:

```cpp
#include <vector>

namespace ferret {

namespace {
constexpr size_t kOpBudget = 10'000'000;
}  // namespace

namespace train_betray_latency_internal {

enum class FillMode { Betray, Control };

std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t train_iters, FillMode mode) {
  const size_t rows = train_iters + 1;
  std::vector<uint32_t> flat(branches * rows, mode == FillMode::Control ? 1U : 0U);
  if (mode == FillMode::Betray) {
    // Training rows are all 1; betrayal row (last) stays 0.
    for (size_t row = 0; row < train_iters; ++row) {
      for (size_t j = 0; j < branches; ++j) {
        flat[row * branches + j] = 1U;
      }
    }
  }
  return flat;
}

}  // namespace train_betray_latency_internal

struct TrainBetrayLatency : Benchmark {
  // ... existing struct ...
};
```

- [ ] **Step 4: Build and run the new tests**

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R TrainBetrayLatency
```
Expected: PASS — original 6 tests + 3 new fill tests = 9 PASS.

- [ ] **Step 5: Commit**

```sh
git add benchmarks/train_betray_latency.cpp tests/test_train_betray_latency.cpp
git commit --no-gpg-sign -m "feat(train_betray_latency): add pattern fill function"
```

---

### Task 5: `emit_kernel` implementation + layout snapshot exposure

Lift the chain shape from `branch_history_footprint`: K data-dependent conditional branches, branch-target = next instruction, NOP padding to enforce minimum spacing, outer loop with `hist_idx` wrapping mod `(M+1)`. Pattern buffer is the `generate_pattern_fill` output stashed on the benchmark instance; `fill_mode_` selects which mode `emit_kernel` uses. Expose a layout snapshot for tests, the same way `branch_history_footprint` does.

**Files:**
- Modify: `benchmarks/train_betray_latency.cpp`
- Modify: `tests/test_train_betray_latency.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_train_betray_latency.cpp`:

```cpp
namespace ferret::train_betray_latency_internal {
struct LayoutSnapshot {
  std::vector<sljit_label*> labels;
  size_t branches;
  size_t spacing;
};
LayoutSnapshot last_layout_snapshot();
}  // namespace ferret::train_betray_latency_internal

TEST(TrainBetrayLatency, RejectsZeroBranches) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  ferret::testing::CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(0)), std::invalid_argument);
}

TEST(TrainBetrayLatency, RejectsNegativeTrainIters) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  ferret::testing::CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(4, /*train_iters=*/-1)),
               std::invalid_argument);
}

TEST(TrainBetrayLatency, RejectsSpacingBytesTooSmall) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  ferret::testing::CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(4, /*train_iters=*/32, /*spacing=*/4)),
               std::invalid_argument);
}

TEST(TrainBetrayLatency, EmitsValidKernelForSmallParams) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  ferret::testing::CompilerHandle ch;
  auto p = make_params(/*branches=*/4, /*train_iters=*/8, /*spacing=*/16);
  ASSERT_NO_THROW(b->emit_kernel(ch.c, p));
  ASSERT_EQ(sljit_get_compiler_error(ch.c), SLJIT_SUCCESS);
  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  ASSERT_NO_THROW(b->verify_layout(ch.c));
  sljit_free_code(code, nullptr);
}

TEST(TrainBetrayLatency, LayoutSnapshotMeetsMinimumSpacing) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  ferret::testing::CompilerHandle ch;
  b->emit_kernel(ch.c, make_params(/*branches=*/4, /*train_iters=*/4, /*spacing=*/16));
  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  b->verify_layout(ch.c);

  auto snap = ferret::train_betray_latency_internal::last_layout_snapshot();
  EXPECT_EQ(snap.branches, 4u);
  EXPECT_EQ(snap.spacing, 16u);
  ASSERT_EQ(snap.labels.size(), 5u);  // branches + 1 chain-exit label
  sljit_uw base = sljit_get_label_addr(snap.labels[0]);
  for (size_t i = 1; i <= snap.branches; ++i) {
    sljit_uw addr = sljit_get_label_addr(snap.labels[i]);
    EXPECT_GE(addr - base, i * snap.spacing) << "site " << i;
  }
  sljit_free_code(code, nullptr);
}
```

- [ ] **Step 2: Run tests to confirm they fail**

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R "TrainBetrayLatency.(Rejects|Emits|Layout)"
```
Expected: FAIL on every new test — emit_kernel still throws "not yet implemented" (and `last_layout_snapshot` is undefined, so some tests fail to link).

- [ ] **Step 3: Implement `emit_kernel`, `verify_layout`, and layout snapshot**

Edit `benchmarks/train_betray_latency.cpp`. Replace the `TrainBetrayLatency` struct (everything from `struct TrainBetrayLatency : Benchmark { ... };` through the `FERRET_BENCHMARK(...)` macro) with the following. This is a near-clone of `branch_history_footprint.cpp` lines 39-227, adapted for the fixed-pattern fill and the `(M+1)`-row layout.

```cpp
struct TrainBetrayLatency;  // forward decl for internal namespace use

namespace {
// Minimum bytes a load + cmp-and-branch can occupy (matches
// branch_history_footprint). 8 on AArch64, 6 on x86_64.
#if defined(__aarch64__) || defined(_M_ARM64)
constexpr size_t kMinSiteBytes = 8;
#elif defined(__x86_64__) || defined(_M_X64)
constexpr size_t kMinSiteBytes = 6;
#else
#error "ferret supports only x86_64 and aarch64"
#endif
}  // namespace

namespace train_betray_latency_internal {

namespace {
const TrainBetrayLatency* g_last_emitted = nullptr;
}  // namespace

LayoutSnapshot last_layout_snapshot();  // defined after struct below

}  // namespace train_betray_latency_internal

struct TrainBetrayLatency : Benchmark {
  using FillMode = train_betray_latency_internal::FillMode;

  // Per-emission state, lives across emit_kernel → verify_layout for one
  // parameter point. flat_ ownership invariant matches branch_history_-
  // footprint: the JIT bakes flat_.data() as SLJIT_IMM, so flat_ must
  // not be reassigned while any kernel that emitted it is still alive.
  // measure_row enforces this by destroying each JittedKernel before
  // building the next one.
  std::vector<uint32_t> flat_;
  std::vector<sljit_label*> last_labels_;
  size_t last_branches_ = 0;
  size_t last_spacing_ = 0;
  FillMode fill_mode_ = FillMode::Betray;

  ~TrainBetrayLatency() override {
    if (train_betray_latency_internal::g_last_emitted == this) {
      train_betray_latency_internal::g_last_emitted = nullptr;
    }
  }

  [[nodiscard]] std::string name() const override { return "train_betray_latency"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {Axis::geom_range("branches", 256, 1024, /*samples_per_octave=*/1)};
  }

  [[nodiscard]] BenchOptions options() const override {
    return {
        BenchOption{.name = "train_iters", .default_value = 32},
        BenchOption{.name = "spacing_bytes", .default_value = 16},
    };
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override {
    return p.get<size_t>("branches");
  }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return compute_iterations(kOpBudget, p.get<size_t>("branches"));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override;
  void verify_layout(sljit_compiler* c) override;
  MeasurementRow measure_row(const Params& p, int reps, int warmup) override;
};

void TrainBetrayLatency::emit_kernel(sljit_compiler* c, const Params& p) {
  auto branches = p.get<size_t>("branches");
  auto train_iters_signed = p.get<int64_t>("train_iters");
  auto spacing = p.get<size_t>("spacing_bytes");

  if (branches < 1) {
    throw std::invalid_argument("branches must be >= 1");
  }
  if (train_iters_signed < 0) {
    throw std::invalid_argument("train_iters must be >= 0; got " + std::to_string(train_iters_signed));
  }
  const auto train_iters = static_cast<size_t>(train_iters_signed);
  if (spacing < kMinSiteBytes) {
    throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                " is smaller than the minimum site encoding (" + std::to_string(kMinSiteBytes) +
                                " bytes) on this architecture");
  }
#if defined(__aarch64__) || defined(_M_ARM64)
  if (spacing % 4 != 0) {
    throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                " must be a multiple of 4 on this architecture");
  }
#endif

  const size_t rows = train_iters + 1;
  const size_t iters = iterations(p);

  flat_ = train_betray_latency_internal::generate_pattern_fill(branches, train_iters, fill_mode_);

  // 3 scratches + 2 saveds, same shape as branch_history_footprint.
  // SLJIT_S0 = flat_base, SLJIT_S1 = hist_idx, SLJIT_R0 = iter counter.
  // SLJIT_R1 = row_ptr, SLJIT_R2 = loaded value.
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/3, /*saved=*/2, /*local_size=*/0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_S0, 0, SLJIT_IMM, reinterpret_cast<sljit_sw>(flat_.data()));
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 0);

  emit_outer_loop(c, SLJIT_R0, iters, [&] {
    // row_ptr = flat_base + hist_idx * (branches * 4)
    sljit_emit_op2(c, SLJIT_MUL, SLJIT_R1, 0, SLJIT_S1, 0, SLJIT_IMM, static_cast<sljit_sw>(branches * 4));
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_S0, 0);

    last_labels_.clear();
    last_labels_.reserve(branches + 1);

    for (size_t j = 0; j < branches; ++j) {
      last_labels_.push_back(sljit_emit_label(c));
      sljit_emit_op1(c, SLJIT_MOV_U32, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R1), static_cast<sljit_sw>(j * 4));
      sljit_jump* jmp = sljit_emit_cmp(c, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R2, 0, SLJIT_IMM, 0);
      sljit_label* after = sljit_emit_label(c);
      sljit_set_label(jmp, after);
      emit_nops(c, spacing - kMinSiteBytes);
    }
    last_labels_.push_back(sljit_emit_label(c));

    // hist_idx = (hist_idx + 1 == rows) ? 0 : hist_idx + 1
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, 1);
    sljit_emit_op2u(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_S1, 0, SLJIT_IMM, static_cast<sljit_sw>(rows));
    sljit_jump* skip_reset = sljit_emit_jump(c, SLJIT_NOT_ZERO);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 0);
    sljit_label* after_wrap = sljit_emit_label(c);
    sljit_set_label(skip_reset, after_wrap);
  });

  sljit_emit_return_void(c);

  last_branches_ = branches;
  last_spacing_ = spacing;
  train_betray_latency_internal::g_last_emitted = this;
}

void TrainBetrayLatency::verify_layout(sljit_compiler* /*c*/) {
  if (last_branches_ == 0 || last_labels_.empty()) {
    return;
  }
  verify_uniform_spacing(last_labels_, last_spacing_, /*strict=*/false, "train_betray_latency");
}

MeasurementRow TrainBetrayLatency::measure_row(const Params& /*p*/, int /*reps*/, int /*warmup*/) {
  throw std::runtime_error("train_betray_latency::measure_row not yet implemented");
}

namespace train_betray_latency_internal {

LayoutSnapshot last_layout_snapshot() {
  if (g_last_emitted == nullptr) {
    return {};
  }
  return {.labels = g_last_emitted->last_labels_,
          .branches = g_last_emitted->last_branches_,
          .spacing = g_last_emitted->last_spacing_};
}

}  // namespace train_betray_latency_internal

FERRET_BENCHMARK("train_betray_latency", TrainBetrayLatency);

}  // namespace ferret
```

Also add `#include "ferret/padding.hpp"` to the includes block (for `emit_nops`).

- [ ] **Step 4: Build and run all tests**

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```
Expected: PASS — all `TrainBetrayLatency.*` tests pass (registry/axes/options/fill from before + 5 new emit/layout tests).

- [ ] **Step 5: Commit**

```sh
git add benchmarks/train_betray_latency.cpp tests/test_train_betray_latency.cpp
git commit --no-gpg-sign -m "feat(train_betray_latency): emit kernel chain and verify layout"
```

---

### Task 6: Differencing helper + unit tests

Pure function that takes two `MeasurementRow`s (B and C) and produces the differenced row. Saturating subtraction handles the case where noise pushes C above B for a given rep. Carries through `iters`, `sites`, `reps`. Unit-testable without any JIT.

**Files:**
- Modify: `benchmarks/train_betray_latency.cpp`
- Modify: `tests/test_train_betray_latency.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_train_betray_latency.cpp`:

```cpp
namespace ferret::train_betray_latency_internal {
ferret::MeasurementRow difference(const ferret::MeasurementRow& betray,
                                  const ferret::MeasurementRow& control,
                                  size_t iters, size_t sites, int reps);
}  // namespace ferret::train_betray_latency_internal

TEST(TrainBetrayLatency, DifferenceReturnsBMinusCWhenBGreater) {
  ferret::MeasurementRow B{.ticks_min = 1000, .ticks_median = 1100};
  ferret::MeasurementRow C{.ticks_min =  300, .ticks_median =  400};
  auto d = ferret::train_betray_latency_internal::difference(B, C, /*iters=*/2, /*sites=*/100, /*reps=*/3);
  EXPECT_EQ(d.ticks_min, 700u);
  EXPECT_EQ(d.ticks_median, 700u);
  EXPECT_EQ(d.iters, 2u);
  EXPECT_EQ(d.sites, 100u);
  EXPECT_EQ(d.reps, 3u);
  EXPECT_FALSE(d.jit_failed);
}

TEST(TrainBetrayLatency, DifferenceSaturatesToZeroWhenCExceedsB) {
  ferret::MeasurementRow B{.ticks_min = 100, .ticks_median = 200};
  ferret::MeasurementRow C{.ticks_min = 150, .ticks_median = 180};
  auto d = ferret::train_betray_latency_internal::difference(B, C, 1, 1, 1);
  EXPECT_EQ(d.ticks_min, 0u);     // saturated (C > B)
  EXPECT_EQ(d.ticks_median, 20u); // not saturated
}

TEST(TrainBetrayLatency, DifferencePropagatesJitFailure) {
  ferret::MeasurementRow B{};
  B.jit_failed = true;
  ferret::MeasurementRow C{.ticks_min = 100};
  auto d = ferret::train_betray_latency_internal::difference(B, C, 1, 1, 1);
  EXPECT_TRUE(d.jit_failed);
}
```

- [ ] **Step 2: Run tests to confirm they fail**

```sh
cmake --build build
```
Expected: build failure — undefined reference to `difference`.

- [ ] **Step 3: Implement `difference`**

Edit `benchmarks/train_betray_latency.cpp`. Inside `namespace train_betray_latency_internal`, after `generate_pattern_fill`, add:

```cpp
MeasurementRow difference(const MeasurementRow& betray, const MeasurementRow& control,
                          size_t iters, size_t sites, int reps) {
  MeasurementRow row;
  if (betray.jit_failed || control.jit_failed) {
    row.jit_failed = true;
    row.iters = iters;
    row.sites = sites;
    row.reps = static_cast<size_t>(reps);
    return row;
  }
  auto sat_sub = [](uint64_t a, uint64_t b) -> uint64_t { return a > b ? a - b : 0; };
  row.ticks_min = sat_sub(betray.ticks_min, control.ticks_min);
  row.ticks_median = sat_sub(betray.ticks_median, control.ticks_median);
  row.iters = iters;
  row.sites = sites;
  row.reps = static_cast<size_t>(reps);
  return row;
}
```

- [ ] **Step 4: Build and run the tests**

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R "TrainBetrayLatency.Difference"
```
Expected: PASS — 3 difference tests pass.

- [ ] **Step 5: Commit**

```sh
git add benchmarks/train_betray_latency.cpp tests/test_train_betray_latency.cpp
git commit --no-gpg-sign -m "feat(train_betray_latency): add differencing helper"
```

---

### Task 7: `measure_row` two-kernel differencing implementation

Compose Tasks 4-6. Build kernel B (FillMode::Betray), time it; destroy it; build kernel C (FillMode::Control), time it; difference the rows. The two `JittedKernel`s must not be alive at the same time — see spec §6 lifetime constraint.

**Files:**
- Modify: `benchmarks/train_betray_latency.cpp`
- Modify: `tests/test_train_betray_latency.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_train_betray_latency.cpp`:

```cpp
#include "ferret/jit.hpp"

TEST(TrainBetrayLatency, MeasureRowReturnsNonNegativeTicksForRealRun) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  // Small K + small M to keep the test fast; we are checking the
  // wiring, not the absolute mispredict cost.
  auto p = make_params(/*branches=*/64, /*train_iters=*/8, /*spacing=*/16);
  auto row = b->measure_row(p, /*reps=*/3, /*warmup=*/1);
  EXPECT_FALSE(row.jit_failed);
  EXPECT_EQ(row.sites, 64u);
  EXPECT_GT(row.iters, 0u);
  // ticks_min is unsigned so the saturating subtract guarantees >= 0;
  // the more interesting check is that it is finite and the run
  // completed without throwing.
  SUCCEED();
}
```

- [ ] **Step 2: Run the test to confirm it fails**

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R MeasureRowReturnsNonNegativeTicksForRealRun
```
Expected: FAIL — `measure_row` throws "not yet implemented".

- [ ] **Step 3: Implement `measure_row`**

Edit `benchmarks/train_betray_latency.cpp`. Add `#include "ferret/jit.hpp"` to the includes. Replace the stub body of `TrainBetrayLatency::measure_row` with the real implementation:

```cpp
MeasurementRow TrainBetrayLatency::measure_row(const Params& p, int reps, int warmup) {
  const auto K = p.get<size_t>("branches");
  const auto iters = iterations(p);
  const runner::MeasureOptions opts{.iters = iters, .sites = K, .reps = reps, .warmup = warmup};

  MeasurementRow B;
  {
    fill_mode_ = FillMode::Betray;
    JittedKernel kB(*this, p);  // emit_kernel installs betray pattern in flat_
    if (!kB.ok()) {
      return train_betray_latency_internal::difference({}, {}, iters, K, reps);
    }
    B = runner::measure(kB.fn(), opts);
  }  // kB destroyed before flat_ is mutated by the next emit_kernel.
  MeasurementRow C;
  {
    fill_mode_ = FillMode::Control;
    JittedKernel kC(*this, p);
    if (!kC.ok()) {
      return train_betray_latency_internal::difference({}, {}, iters, K, reps);
    }
    C = runner::measure(kC.fn(), opts);
  }
  return train_betray_latency_internal::difference(B, C, iters, K, reps);
}
```

(The `difference({}, {}, ...)` call on JIT failure returns a row with `jit_failed=true` — `difference` checks `B.jit_failed || C.jit_failed`. The default-constructed `MeasurementRow` does not have `jit_failed=true` though, so we must pass a row that does. Tighten the JIT-failure path:)

Replace the two `if (!kX.ok())` branches with:

```cpp
    if (!kB.ok()) {
      MeasurementRow fail;
      fail.jit_failed = true;
      return train_betray_latency_internal::difference(fail, {}, iters, K, reps);
    }
```

and the analogous version for `kC`.

- [ ] **Step 4: Run the test**

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R TrainBetrayLatency
```
Expected: PASS — all `TrainBetrayLatency.*` tests pass.

- [ ] **Step 5: Run the full suite**

```sh
ctest --test-dir build --output-on-failure
```
Expected: PASS.

- [ ] **Step 6: Commit**

```sh
git add benchmarks/train_betray_latency.cpp tests/test_train_betray_latency.cpp
git commit --no-gpg-sign -m "feat(train_betray_latency): implement two-kernel differencing in measure_row"
```

---

### Task 8: Integration test — end-to-end run via `build/ferret`

Smoke-test the full pipeline: CLI → sweep → measure_row → CSV. Asserts the binary exits 0 and the CSV has the expected columns/rows for a tiny sweep.

**Files:**
- Modify: `tests/test_integration.cpp`

- [ ] **Step 1: Write the failing tests**

Append to `tests/test_integration.cpp` (place after the existing `BranchHistoryFootprint*` integration tests, around line 180). Reuses the file's existing `run`, `slurp`, and `TempFileGuard` helpers — same shape as `BranchHistoryFootprintProducesExpectedRowCount`:

```cpp
TEST(Integration, TrainBetrayLatencyProducesExpectedRowCount) {
  auto out = std::filesystem::temp_directory_path() / "ferret_misp.csv";
  TempFileGuard guard_out{out};
  // branches ∈ {256, 512} → 2 data rows. train_iters small so the test
  // finishes quickly on CI; the absolute number doesn't matter for a
  // wiring smoke test.
  std::string cmd = std::string(FERRET_BINARY) +
                    " run train_betray_latency"
                    " --branches=256,512"
                    " --train_iters=8"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 3u);  // header + 2 data rows
  EXPECT_EQ(contents.find(",,\n"), std::string::npos);
}

TEST(Integration, TrainBetrayLatencyHeaderHasExpectedColumns) {
  auto out = std::filesystem::temp_directory_path() / "ferret_misp_hdr.csv";
  TempFileGuard guard_out{out};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run train_betray_latency"
                    " --branches=256"
                    " --train_iters=8"
                    " --reps=2 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  EXPECT_NE(contents.find("branches"), std::string::npos);
  EXPECT_NE(contents.find("train_iters"), std::string::npos);
  EXPECT_NE(contents.find("spacing_bytes"), std::string::npos);
  EXPECT_NE(contents.find("ns_per_site_min"), std::string::npos);
}
```

- [ ] **Step 2: Run the new tests**

```sh
cmake --build build
ctest --test-dir build --output-on-failure -R TrainBetrayLatency
```
Expected: PASS — both integration tests pass. The benchmark is fully implemented by Task 7, so end-to-end works. If either test fails, the failure points at a real wiring bug — fix the root cause (not the test).

- [ ] **Step 3: Run the full suite**

```sh
ctest --test-dir build --output-on-failure
```
Expected: PASS.

- [ ] **Step 4: Commit**

```sh
git add tests/test_integration.cpp
git commit --no-gpg-sign -m "test(train_betray_latency): add end-to-end integration test"
```

---

### Task 9: Per-benchmark documentation page

Create `docs/benchmarks/train_betray_latency.md` following the format of `docs/benchmarks/branch_history_footprint.md`.

**Files:**
- Create: `docs/benchmarks/train_betray_latency.md`

- [ ] **Step 1: Reference the existing doc structure**

```sh
cat docs/benchmarks/branch_history_footprint.md | head -40
```
Sections (in order): one-paragraph intro, "Kernel structure" with ASCII art, "Per-benchmark options" table, "CLI surface" table, "Reading the curves", "Caveats", "Related docs".

- [ ] **Step 2: Write the doc page**

Create `docs/benchmarks/train_betray_latency.md`:

````markdown
# `train_betray_latency` — per-branch mispredict penalty

`K` data-dependent conditional branches in a chain, branch-target =
next instruction (so taken / not-taken converge architecturally —
direction prediction is the sole measured variable). Per outer-loop
iteration, the chain reads its outcomes from a per-branch row of a
flat `uint32_t` buffer indexed by a history position that cycles
through `M+1` rows. `M` of those rows are all-1s (training: always-
taken, saturates the direction predictor on every PC); the last is
all-0s (betrayal: every branch in the chain mispredicts at once).

Per parameter point the benchmark emits **two** kernels — a betray
kernel with the M-of-1s + 1-of-0s pattern, and a control kernel with
all `M+1` rows of 1s — times each, and reports the difference as
ticks per mispredict. With `--freq`, the CSV's `cycles_per_site` is
*cycles per mispredict*; without it, `ns_per_site` is ns per
mispredict.

## Kernel structure

```
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
- The betray kernel and control kernel differ only in the *contents*
  of `flat[]`. They are emitted as two distinct JITted functions so
  the differencing residual (B - C) reflects the mispredict cost of
  the K betrayed branches, with everything else (chain shape, outer-
  loop tax, training-round cost) cancelling.

## Per-benchmark options

| flag                  | meaning                                                  |
| --------------------- | -------------------------------------------------------- |
| `--train_iters=M`     | Training rounds per (M+1)-iter cycle. Default 32.       |
| `--spacing_bytes=N`   | Minimum per-site PC stride. Min 8 (AArch64) / 6 (x86_64). Default 16. |

## CLI surface

| flag                       | meaning                                                                |
| -------------------------- | ---------------------------------------------------------------------- |
| `--branches=A..B`          | Geometric sweep, default `k=1`, e.g. `256..1024`.                       |
| `--branches=A..B@k`        | Geometric sweep with `k` samples per octave.                            |
| `--branches=v1,v2,…`       | Explicit list.                                                          |
| `--train_iters=N`          | See above. Default 32.                                                  |
| `--spacing_bytes=N`        | See above. Default 16.                                                  |
| `--seed=…`                 | Accepted but unused (pattern is deterministic).                         |

See [`../cli.md`](../cli.md) for global flags.

## Reading the curves

Plot `branches` on X, `ns_per_site_min` (or `cycles_per_site_min` with
`--freq`) on Y:

```sh
python3 scripts/plot.py line /tmp/misp.csv --out=/tmp/misp.png
```

- **Flat curve at ~12–25 cycles**: the headline per-mispredict
  penalty for this core. Read it off any point in the flat region.
- **Curve bends downward as K grows**: the chain outgrew what the
  predictor confidently trained, so fewer than `K` betrayal branches
  actually mispredicted. The reported per-mispredict cost is the
  *average* per-betrayal-branch cost — an underestimate.
- **Curve bends upward as K grows**: investigate. Most likely
  candidates are iL1 / iTLB capacity effects at the larger chain
  byte sizes.

## Caveats

- **Differencing-vs-noise floor.** Saturating subtract clamps `B - C`
  to 0 when noise pushes a single rep's control above its betray. If
  reported `ticks_min == 0` across the sweep, bump `--reps`.
- **`M=32` is a defensible default, not a proof.** The construction
  saturates bimodal counters in ~2-3 updates and gives TAGE many
  rounds of confident global history. If a future core shows non-flat
  K-sweeps, `--train_iters` can be raised as a diagnostic (though it
  is not a documented sweep axis).
- **No HW counter cross-check.** The "100% mispredict" claim rests on
  train-and-betray construction; the K-sweep is the only diagnostic.
- **Apple Silicon pinning.** Same caveat as every other ferret
  benchmark — probe and target may land on different P-cores.

## Related docs

- Design spec:
  [`../superpowers/specs/2026-05-27-mispredict-latency-design.md`](../superpowers/specs/2026-05-27-mispredict-latency-design.md).
- Sister benchmark (capacity, not latency):
  [`branch_history_footprint`](branch_history_footprint.md).
- Project two-step workflow: [project README](../../README.md).
````

- [ ] **Step 3: Commit**

```sh
git add docs/benchmarks/train_betray_latency.md
git commit --no-gpg-sign -m "docs(train_betray_latency): add benchmark documentation page"
```

---

### Task 10: README table update

Add the new benchmark to the benchmarks table in `README.md`.

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add the row to the table**

Edit `README.md`. Find the benchmarks table (line 78-83 in the current README) and add a new row at the end:

```markdown
| [`train_betray_latency`](docs/benchmarks/train_betray_latency.md)                 | per-branch mispredict penalty (train-and-betray)|
```

The full table after edit:

```markdown
| Name                                                                          | Targets                                         |
| ----------------------------------------------------------------------------- | ----------------------------------------------- |
| [`dependent_chain_throughput`](docs/benchmarks/dependent_chain_throughput.md) | running core frequency / 1-IPC baseline         |
| [`direct_branch_footprint`](docs/benchmarks/direct_branch_footprint.md)       | direct-jump BTB capacity                        |
| [`nested_call_depth`](docs/benchmarks/nested_call_depth.md)                   | Return Address Stack (RAS) depth                |
| [`branch_history_footprint`](docs/benchmarks/branch_history_footprint.md)     | conditional-branch direction-predictor capacity |
| [`train_betray_latency`](docs/benchmarks/train_betray_latency.md)                 | per-branch mispredict penalty (train-and-betray)|
```

- [ ] **Step 2: Verify the benchmark appears in `ferret list`**

```sh
build/ferret list
```
Expected output includes `train_betray_latency`.

- [ ] **Step 3: Commit**

```sh
git add README.md
git commit --no-gpg-sign -m "docs(readme): list train_betray_latency in benchmarks table"
```

---

## Final verification

After all tasks complete, run the full test + a real one-shot benchmark sweep:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
build/ferret run train_betray_latency --branches=256..1024 --reps=5 --warmup=2 --out=/tmp/misp_final.csv
column -ts, /tmp/misp_final.csv
```

Sanity-check the printed table: `ns_per_site_min` should be monotonically near-flat across K=256/512/1024 (not strictly monotone — noise — but order-of-magnitude flat). On a 4 GHz x86 core, expect roughly 3-6 ns per mispredict (~15-22 cycles). On Apple Silicon P-core, expect roughly 3-5 ns (~12-18 cycles).

A wildly non-flat K-sweep (e.g. ten-fold drop from K=256 to K=1024) means the chain outgrew predictor confidence — the spec's expected failure mode for very large K. A constant zero across the sweep means noise dominated the differencing — bump `--reps` to 9 or 11 and rerun.

# bias_conditional_branch_count Implementation Plan

> **Post-rebase note (2026-05-20):** This plan was authored against the
> pre-`bench_helpers` codebase and proposed a dedicated
> `branch_chain_emit` module. After rebasing onto `origin/main` (which
> landed `include/ferret/bench_helpers.hpp` and refactored every
> existing benchmark to use it), the actual implementation drops the
> proposed module and follows the upstream pattern instead:
> `emit_outer_loop` + `compute_iterations` + `verify_uniform_spacing`
> + `mix_seed`, with the per-site chain inlined in the benchmark file
> the same way `branch_history_footprint` does. Task 1 (the
> `branch_chain_emit` extraction) becomes a no-op. Tasks 2–7 still
> describe the right *shape* of work, but the file paths and shared
> helper names reference upstream `bench_helpers` instead of the
> proposed module.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add ferret's fifth benchmark, `bias_conditional_branch_count`, which sweeps `(branches, total_outcomes)` and emits mixed-direction biased conditional branches whose long aperiodic outcome patterns expose the **SC bias-table capacity** in TAGE-SC-L–style direction predictors.

**Architecture:** The branch-chain sljit emitter currently inlined in `benchmarks/branch_history_footprint.cpp` is extracted into a shared helper (`include/ferret/branch_chain_emit.hpp` + `src/branch_chain_emit.cpp`) consumed by both `branch_history_footprint` and the new `bias_conditional_branch_count`. The new benchmark differs only in its pattern-fill routine (per-branch direction assignment + Bernoulli(`bias_pct`) outcomes) and its axes/options. Per-bench options: `bias_pct` (default 95), `nt_branch_pct` (default 50), `spacing_bytes` (default 16). Total buffer size is bounded by `total_outcomes` (≤ 4 MB at default-axis max).

**Tech Stack:** C++20, sljit (JIT IR), GoogleTest, CMake. Uses the existing per-site instruction sequences (`SLJIT_MOV_U32 + SLJIT_NOT_EQUAL` cmp-jump on both arches, via sljit high-level ops — same as `branch_history_footprint`).

**Spec:** [`docs/superpowers/specs/2026-05-20-bias-conditional-branch-count-design.md`](../specs/2026-05-20-bias-conditional-branch-count-design.md)

**Worktree:** Work happens in the existing checkout at `/Users/easton/WorkingSpace/project/ferret/feat/sc-bias-table` on branch `sc-bias-table` — no new worktree.

---

## File Map

| Path | Action | Responsibility |
| ---- | ------ | -------------- |
| `include/ferret/branch_chain_emit.hpp` | Create | Public declaration of the shared branch-chain emitter (config + result structs, `emit_branch_chain`, `verify_branch_chain_layout`, `branch_chain_min_site_bytes`) |
| `src/branch_chain_emit.cpp` | Create | Implementation of the shared emitter (moved verbatim from `branch_history_footprint.cpp::emit_kernel` + `verify_layout`) |
| `benchmarks/branch_history_footprint.cpp` | Modify | Drop the inlined emitter; call shared helper; keep the pattern-fill (`generate_pattern_fill`) and the per-instance state |
| `benchmarks/bias_conditional_branch_count.cpp` | Create | New benchmark: axes (`branches`, `total_outcomes`), options (`bias_pct`, `nt_branch_pct`, `spacing_bytes`), direction-assignment + outcome-fill routines, `emit_kernel` calling shared helper |
| `tests/test_bias_conditional_branch_count.cpp` | Create | Unit tests for axes/options/validation/direction-assignment/outcome-distribution/layout |
| `tests/test_branch_history_footprint.cpp` | Verify | Existing tests must still pass after the emitter extraction (lock-in regression guard) |
| `tests/test_integration.cpp` | Modify | Append `Integration.BiasConditionalBranchCount*` smoke tests |
| `CMakeLists.txt` | Modify | Add `src/branch_chain_emit.cpp` to `ferret_core` source list and `benchmarks/bias_conditional_branch_count.cpp` to the `ferret` executable source list |
| `tests/CMakeLists.txt` | Modify | Add `test_bias_conditional_branch_count` target |
| `docs/benchmarks/bias_conditional_branch_count.md` | Create | Per-benchmark doc page (kernel structure, options, reading the surface) |
| `README.md` | Modify | Add row to the benchmarks table |

---

## Pre-flight (do once before Task 1)

- [ ] **Verify branch and clean tree**

```bash
git status
git branch --show-current
```

Expected: `On branch sc-bias-table`, working tree clean.

- [ ] **Verify the spec exists and the build is green at baseline**

```bash
test -f docs/superpowers/specs/2026-05-20-bias-conditional-branch-count-design.md && echo "spec OK"
nix develop --command cmake -S . -B build -GNinja
nix develop --command cmake --build build
nix develop --command ctest --test-dir build --output-on-failure
```

Expected: `spec OK`, clean build, all existing tests pass. If tests fail at baseline, **stop and surface to user** — do not proceed.

---

### Task 1: Extract the shared branch-chain emitter

The current `benchmarks/branch_history_footprint.cpp::emit_kernel` contains the sljit chain emission (prologue, row_ptr update, per-site load+cmp+jmp, history wrap, loop tail, return). Move that into a shared helper. Keep pattern generation in the benchmark.

**Files:**
- Create: `include/ferret/branch_chain_emit.hpp`
- Create: `src/branch_chain_emit.cpp`
- Modify: `benchmarks/branch_history_footprint.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Create the header**

Create `include/ferret/branch_chain_emit.hpp`:

```cpp
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

struct sljit_compiler;
struct sljit_label;

namespace ferret {

struct BranchChainEmitConfig {
  const uint32_t* flat_base;
  std::size_t branches;
  std::size_t pattern_period;
  std::size_t spacing_bytes;
  std::size_t iterations;
};

struct BranchChainEmitResult {
  std::vector<sljit_label*> site_labels;  // size == branches + 1 (last entry = chain exit)
};

// Emits one outer-loop branch-chain kernel into `c`. Caller has already
// validated that `flat_base != nullptr`, `branches >= 1`,
// `pattern_period >= 1`, `iterations >= 1`, and
// `spacing_bytes >= branch_chain_min_site_bytes()`. Mutates `c` only after
// argument checks pass.
BranchChainEmitResult emit_branch_chain(sljit_compiler* c,
                                         const BranchChainEmitConfig& cfg);

// Post-codegen verification. `result` is the value returned by
// `emit_branch_chain` for the same compiler. Throws std::runtime_error if
// any site lies closer than `cfg.spacing_bytes` from the previous site.
void verify_branch_chain_layout(const BranchChainEmitResult& result,
                                 std::size_t branches,
                                 std::size_t spacing_bytes);

// Floor on the per-site byte width across all sljit encodings on this arch.
// Used by benchmark validation: spacing_bytes must be >= this value.
std::size_t branch_chain_min_site_bytes();

}  // namespace ferret
```

- [ ] **Step 2: Create the implementation**

Create `src/branch_chain_emit.cpp`:

```cpp
#include "ferret/branch_chain_emit.hpp"

#include <stdexcept>
#include <string>

extern "C" {
#include <sljitLir.h>
}

#include "ferret/padding.hpp"

namespace ferret {

namespace {

#if defined(__aarch64__) || defined(_M_ARM64)
constexpr std::size_t kMinSiteBytes = 8;
#elif defined(__x86_64__) || defined(_M_X64)
constexpr std::size_t kMinSiteBytes = 6;
#else
#error "ferret v1 supports only x86_64 and aarch64"
#endif

}  // namespace

std::size_t branch_chain_min_site_bytes() { return kMinSiteBytes; }

BranchChainEmitResult emit_branch_chain(sljit_compiler* c,
                                         const BranchChainEmitConfig& cfg) {
  BranchChainEmitResult result;

  // sljit prologue: 3 scratches + 2 saveds.
  // SLJIT_S0 = flat_base, SLJIT_S1 = hist_idx, SLJIT_R0 = iter counter,
  // SLJIT_R1 = row_ptr, SLJIT_R2 = loaded value.
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/3, /*saved=*/2, /*local_size=*/0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_S0, 0, SLJIT_IMM, reinterpret_cast<sljit_sw>(cfg.flat_base));
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, static_cast<sljit_sw>(cfg.iterations));

  sljit_label* loop_top = sljit_emit_label(c);

  // row_ptr = flat_base + hist_idx * (branches * 4)
  sljit_emit_op2(c, SLJIT_MUL, SLJIT_R1, 0, SLJIT_S1, 0, SLJIT_IMM, static_cast<sljit_sw>(cfg.branches * 4));
  sljit_emit_op2(c, SLJIT_ADD, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_S0, 0);

  result.site_labels.reserve(cfg.branches + 1);

  for (std::size_t j = 0; j < cfg.branches; ++j) {
    result.site_labels.push_back(sljit_emit_label(c));

    sljit_emit_op1(c, SLJIT_MOV_U32, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R1), static_cast<sljit_sw>(j * 4));
    sljit_jump* jmp = sljit_emit_cmp(c, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R2, 0, SLJIT_IMM, 0);
    sljit_label* after = sljit_emit_label(c);
    sljit_set_label(jmp, after);

    emit_nops(c, cfg.spacing_bytes - kMinSiteBytes);
  }
  result.site_labels.push_back(sljit_emit_label(c));

  // hist_idx = (hist_idx + 1 == pattern_period) ? 0 : hist_idx + 1
  sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, 1);
  sljit_emit_op2u(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_S1, 0, SLJIT_IMM, static_cast<sljit_sw>(cfg.pattern_period));
  sljit_jump* skip_reset = sljit_emit_jump(c, SLJIT_NOT_ZERO);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 0);
  sljit_label* after_wrap = sljit_emit_label(c);
  sljit_set_label(skip_reset, after_wrap);

  // dec iters; back-edge.
  sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
  sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
  sljit_set_label(back, loop_top);

  sljit_emit_return_void(c);

  return result;
}

void verify_branch_chain_layout(const BranchChainEmitResult& result,
                                 std::size_t branches,
                                 std::size_t spacing_bytes) {
  if (branches == 0 || result.site_labels.empty()) {
    return;
  }
  sljit_uw base = sljit_get_label_addr(result.site_labels[0]);
  for (std::size_t i = 1; i <= branches; ++i) {
    sljit_uw addr = sljit_get_label_addr(result.site_labels[i]);
    auto actual = static_cast<std::size_t>(addr - base);
    std::size_t expected_min = i * spacing_bytes;
    if (actual < expected_min) {
      throw std::runtime_error("branch_chain: site " + std::to_string(i) + " at offset " +
                               std::to_string(actual) + ", expected at least " +
                               std::to_string(expected_min));
    }
  }
}

}  // namespace ferret
```

- [ ] **Step 3: Add `src/branch_chain_emit.cpp` to `ferret_core`**

Edit `CMakeLists.txt`. Find the `ferret_core` library definition (the `add_library(ferret_core ...)` block, around line 150). Append `src/branch_chain_emit.cpp` to its source list, alphabetically near `src/axis.cpp` / `src/cli_axis.cpp`.

For example, if the current list is:

```cmake
add_library(ferret_core
  src/axis.cpp
  src/benchmark_registry.cpp
  src/cli_axis.cpp
  ...
  src/permute.cpp
)
```

insert `src/branch_chain_emit.cpp` between `src/benchmark_registry.cpp` and `src/cli_axis.cpp` so the list stays alphabetical.

- [ ] **Step 4: Refactor `branch_history_footprint.cpp` to call the helper**

Edit `benchmarks/branch_history_footprint.cpp`. Replace its `emit_kernel` and `verify_layout` with calls into the shared helper. Keep `generate_pattern_fill`, `LayoutSnapshot`, `last_layout_snapshot`, the per-instance state, and the validation throws.

Full new contents of the file:

```cpp
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "ferret/benchmark.hpp"
#include "ferret/branch_chain_emit.hpp"

namespace ferret {

struct BranchHistoryFootprint;

namespace branch_history_footprint_internal {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t history_len, int64_t pattern, uint64_t seed) {
  std::vector<uint32_t> flat(branches * history_len, 0U);
  if (pattern == 0) {
    return flat;
  }
  uint64_t mixed = seed ^ (static_cast<uint64_t>(branches) * 0x9E3779B97F4A7C15ULL) ^
                   (static_cast<uint64_t>(history_len) * 0xBF58476D1CE4E5B9ULL);
  std::mt19937_64 rng(mixed);
  for (auto& v : flat) {
    v = static_cast<uint32_t>(rng() & 1U);
  }
  return flat;
}

struct LayoutSnapshot {
  std::vector<sljit_label*> labels;
  size_t branches;
  size_t spacing;
};

namespace {
const BranchHistoryFootprint* g_last_emitted = nullptr;
}  // namespace

LayoutSnapshot last_layout_snapshot();

}  // namespace branch_history_footprint_internal

struct BranchHistoryFootprint : Benchmark {
  std::vector<uint32_t> flat_;
  BranchChainEmitResult last_emit_;
  size_t last_branches_ = 0;
  size_t last_spacing_ = 0;

  [[nodiscard]] std::string name() const override { return "branch_history_footprint"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::geom_range("branches", 1, 1 << 9, /*samples_per_octave=*/1),
        Axis::geom_range("history_len", 4, 1 << 12, /*samples_per_octave=*/1),
    };
  }

  [[nodiscard]] BenchOptions options() const override {
    return {
        BenchOption{.name = "pattern", .default_value = 1},
        BenchOption{.name = "spacing_bytes", .default_value = 16},
    };
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("branches"); }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return std::max<size_t>(1, 10'000'000 / p.get<size_t>("branches"));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override;
  void verify_layout(sljit_compiler* c) override;
};

void BranchHistoryFootprint::emit_kernel(sljit_compiler* c, const Params& p) {
  auto branches = p.get<size_t>("branches");
  auto history_len = p.get<size_t>("history_len");
  auto pattern = p.get<int64_t>("pattern");
  auto spacing = p.get<size_t>("spacing_bytes");
  auto seed = static_cast<uint64_t>(p.get<int64_t>("seed"));

  if (branches < 1) {
    throw std::invalid_argument("branches must be >= 1");
  }
  if (history_len < 1) {
    throw std::invalid_argument("history_len must be >= 1");
  }
  if (pattern != 0 && pattern != 1) {
    throw std::invalid_argument("pattern must be 0 (zero) or 1 (random); got " + std::to_string(pattern));
  }
  if (spacing < branch_chain_min_site_bytes()) {
    throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                " is smaller than the minimum site encoding (" +
                                std::to_string(branch_chain_min_site_bytes()) +
                                " bytes) on this architecture");
  }

  size_t iters = iterations(p);

  flat_ = branch_history_footprint_internal::generate_pattern_fill(branches, history_len, pattern, seed);

  last_emit_ = emit_branch_chain(c, BranchChainEmitConfig{
                                        .flat_base = flat_.data(),
                                        .branches = branches,
                                        .pattern_period = history_len,
                                        .spacing_bytes = spacing,
                                        .iterations = iters,
                                    });

  last_branches_ = branches;
  last_spacing_ = spacing;
  branch_history_footprint_internal::g_last_emitted = this;
}

void BranchHistoryFootprint::verify_layout(sljit_compiler* /*c*/) {
  if (last_branches_ == 0 || last_emit_.site_labels.empty()) {
    return;
  }
  verify_branch_chain_layout(last_emit_, last_branches_, last_spacing_);
}

namespace branch_history_footprint_internal {

LayoutSnapshot last_layout_snapshot() {
  if (g_last_emitted == nullptr) {
    return {};
  }
  return {.labels = g_last_emitted->last_emit_.site_labels,
          .branches = g_last_emitted->last_branches_,
          .spacing = g_last_emitted->last_spacing_};
}

}  // namespace branch_history_footprint_internal

FERRET_BENCHMARK("branch_history_footprint", BranchHistoryFootprint);

}  // namespace ferret
```

Note: `last_emit_` is a public field of `BranchHistoryFootprint` so the snapshot accessor can read `site_labels` without a friend declaration. The `g_last_emitted` mechanism is preserved verbatim.

- [ ] **Step 5: Build and run existing tests as the lock-in regression guard**

```bash
nix develop --command cmake --build build
nix develop --command ctest --test-dir build --output-on-failure
```

Expected: all existing tests pass, including every `BranchHistoryFootprint.*` and `Integration.BranchHistoryFootprint*` test. If anything fails, **stop and fix before continuing** — the refactor must be observably no-op.

- [ ] **Step 6: Commit**

```bash
git add include/ferret/branch_chain_emit.hpp src/branch_chain_emit.cpp benchmarks/branch_history_footprint.cpp CMakeLists.txt
git commit --no-gpg-sign -m "$(cat <<'EOF'
refactor(branch_chain_emit): extract shared sljit branch-chain emitter

Factors emit_kernel/verify_layout out of branch_history_footprint into
a reusable helper consumed by both that benchmark and the upcoming
bias_conditional_branch_count benchmark.
EOF
)"
```

---

### Task 2: Scaffold the new benchmark + first registry test

**Files:**
- Create: `benchmarks/bias_conditional_branch_count.cpp`
- Create: `tests/test_bias_conditional_branch_count.cpp`
- Modify: `tests/CMakeLists.txt`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Write the failing registry test**

Create `tests/test_bias_conditional_branch_count.cpp`:

```cpp
#include <gtest/gtest.h>

#include "ferret/benchmark.hpp"

TEST(BiasConditionalBranchCount, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "bias_conditional_branch_count");
}
```

- [ ] **Step 2: Add the test target to CMake**

Edit `tests/CMakeLists.txt`. Find the `test_branch_history_footprint` block (around line 105) and add **after** it:

```cmake
add_executable(test_bias_conditional_branch_count
  test_bias_conditional_branch_count.cpp
  ../benchmarks/bias_conditional_branch_count.cpp
)
target_link_libraries(test_bias_conditional_branch_count PRIVATE
  ferret_core
  sljit::sljit
  GTest::gtest GTest::gtest_main
)
gtest_discover_tests(test_bias_conditional_branch_count)
```

- [ ] **Step 3: Create a stub benchmark that registers**

Create `benchmarks/bias_conditional_branch_count.cpp`:

```cpp
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "ferret/benchmark.hpp"
#include "ferret/branch_chain_emit.hpp"

namespace ferret {

struct BiasConditionalBranchCount : Benchmark {
  std::vector<uint32_t> flat_;
  BranchChainEmitResult last_emit_;
  size_t last_branches_ = 0;
  size_t last_spacing_ = 0;

  [[nodiscard]] std::string name() const override { return "bias_conditional_branch_count"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::geom_range("branches", 1, 1 << 13, /*samples_per_octave=*/1),
        Axis::geom_range("total_outcomes", 1 << 13, 1 << 20, /*samples_per_octave=*/1),
    };
  }

  [[nodiscard]] BenchOptions options() const override {
    return {
        BenchOption{.name = "bias_pct", .default_value = 95},
        BenchOption{.name = "nt_branch_pct", .default_value = 50},
        BenchOption{.name = "spacing_bytes", .default_value = 16},
    };
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("branches"); }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return std::max<size_t>(1, 10'000'000 / p.get<size_t>("branches"));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override {
    (void)c;
    (void)p;
    throw std::runtime_error("not implemented yet");
  }
};

FERRET_BENCHMARK("bias_conditional_branch_count", BiasConditionalBranchCount);

}  // namespace ferret
```

- [ ] **Step 4: Wire into the `ferret` executable**

Edit `CMakeLists.txt`. Find `add_executable(ferret ...)` (around line 167). Add `benchmarks/bias_conditional_branch_count.cpp` alphabetically:

```cmake
add_executable(ferret
  src/main.cpp
  benchmarks/bias_conditional_branch_count.cpp
  benchmarks/branch_history_footprint.cpp
  benchmarks/dependent_chain_throughput.cpp
  benchmarks/direct_branch_footprint.cpp
  benchmarks/nested_call_depth.cpp
)
```

- [ ] **Step 5: Build and run the registry test**

```bash
nix develop --command cmake --build build
nix develop --command ctest --test-dir build -R BiasConditionalBranchCount --output-on-failure
```

Expected: `BiasConditionalBranchCount.RegistryLookupReturnsBenchmark` passes; no other behavior is wired yet.

- [ ] **Step 6: Commit**

```bash
git add benchmarks/bias_conditional_branch_count.cpp tests/test_bias_conditional_branch_count.cpp tests/CMakeLists.txt CMakeLists.txt
git commit --no-gpg-sign -m "feat(bias_conditional_branch_count): scaffold benchmark + registry test"
```

---

### Task 3: Lock in axes, options, sites_per_kernel, iterations

**Files:**
- Modify: `tests/test_bias_conditional_branch_count.cpp`

- [ ] **Step 1: Append the axes/options test suite**

Append to `tests/test_bias_conditional_branch_count.cpp` (above the existing single test, helpers go in an anonymous namespace at the top):

```cpp
#include "ferret/params.hpp"

namespace {
ferret::Params make_params(int64_t branches, int64_t total_outcomes, int64_t bias = 95, int64_t nt = 50,
                            int64_t spacing = 16) {
  ferret::Params p;
  p.set("branches", branches);
  p.set("total_outcomes", total_outcomes);
  p.set("bias_pct", bias);
  p.set("nt_branch_pct", nt);
  p.set("spacing_bytes", spacing);
  p.set("seed", 1);
  return p;
}
const ferret::BenchOption* find_option(const ferret::BenchOptions& opts, const std::string& name) {
  for (const auto& o : opts) {
    if (o.name == name) return &o;
  }
  return nullptr;
}
}  // namespace

TEST(BiasConditionalBranchCount, ExposesBranchesAndTotalOutcomesAxes) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  ASSERT_EQ(axes.size(), 2u);
  EXPECT_EQ(axes[0].name(), "branches");
  EXPECT_EQ(axes[1].name(), "total_outcomes");
}

TEST(BiasConditionalBranchCount, BranchesAxisExpansionMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[0].expand();
  // 1..8192 with k=1: {1,2,4,8,16,32,64,128,256,512,1024,2048,4096,8192} = 14 points.
  EXPECT_EQ(vs.size(), 14u);
  EXPECT_EQ(vs.front(), 1);
  EXPECT_EQ(vs.back(), 8192);
}

TEST(BiasConditionalBranchCount, TotalOutcomesAxisExpansionMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[1].expand();
  // 8192..1048576 with k=1: 8 points {8192, 16384, ..., 1048576}.
  EXPECT_EQ(vs.size(), 8u);
  EXPECT_EQ(vs.front(), 8192);
  EXPECT_EQ(vs.back(), 1048576);
}

TEST(BiasConditionalBranchCount, DefaultAxisGridIsValidationClean) {
  // total_outcomes_min must be >= branches_max so pattern_period >= 1 at
  // every default Cartesian point. spec §5.1.
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  auto branches = axes[0].expand();
  auto totals = axes[1].expand();
  EXPECT_GE(totals.front(), branches.back());
}

TEST(BiasConditionalBranchCount, ExposesAllThreeOptions) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 3u);
  EXPECT_EQ(find_option(opts, "bias_pct")->default_value, 95);
  EXPECT_EQ(find_option(opts, "nt_branch_pct")->default_value, 50);
  EXPECT_EQ(find_option(opts, "spacing_bytes")->default_value, 16);
}

TEST(BiasConditionalBranchCount, SitesPerKernelEqualsBranches) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->sites_per_kernel(make_params(32, 65536)), 32u);
  EXPECT_EQ(b->sites_per_kernel(make_params(1, 8192)), 1u);
}

TEST(BiasConditionalBranchCount, IterationsAmortizesAtTenMillionSites) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->iterations(make_params(1, 8192)), 10'000'000u);
  EXPECT_EQ(b->iterations(make_params(100, 65536)), 100'000u);
  EXPECT_EQ(b->iterations(make_params(10'000'001, 65536)), 1u);
}
```

- [ ] **Step 2: Run the new tests; they should pass already (the stub already declares the axes/options)**

```bash
nix develop --command cmake --build build
nix develop --command ctest --test-dir build -R BiasConditionalBranchCount --output-on-failure
```

Expected: all `BiasConditionalBranchCount.*` axis/options tests pass. Validation/fill/emit tests are coming in Task 4+.

- [ ] **Step 3: Commit**

```bash
git add tests/test_bias_conditional_branch_count.cpp
git commit --no-gpg-sign -m "test(bias_conditional_branch_count): lock in axes, options, sites/iterations"
```

---

### Task 4: Add internal direction-assignment + outcome-fill routines

Spec §7.1–§7.3. Two independent RNG streams: one for direction assignment (stable across `total_outcomes`), one for outcome fill.

**Files:**
- Modify: `benchmarks/bias_conditional_branch_count.cpp`
- Modify: `tests/test_bias_conditional_branch_count.cpp`

- [ ] **Step 1: Write failing tests for the direction-assignment helper**

Add to `tests/test_bias_conditional_branch_count.cpp` (top of file, after existing helpers):

```cpp
#include <cstdint>
#include <vector>

namespace ferret::bias_conditional_branch_count_internal {
std::vector<uint8_t> assign_directions(size_t branches, int64_t nt_branch_pct, uint64_t seed);
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t pattern_period, int64_t bias_pct,
                                             int64_t nt_branch_pct, uint64_t seed);
}  // namespace ferret::bias_conditional_branch_count_internal
```

Then add the tests:

```cpp
TEST(BiasConditionalBranchCount, DirectionAssignmentHonorsNtBranchPct) {
  auto dirs = ferret::bias_conditional_branch_count_internal::assign_directions(
      /*branches=*/1024, /*nt_branch_pct=*/50, /*seed=*/1);
  ASSERT_EQ(dirs.size(), 1024u);
  size_t nt = 0;
  for (uint8_t d : dirs) {
    EXPECT_TRUE(d == 0u || d == 1u);
    nt += d;
  }
  EXPECT_EQ(nt, 512u);  // 1024 * 50 / 100
}

TEST(BiasConditionalBranchCount, DirectionAssignmentDeterministicForSameSeed) {
  auto a = ferret::bias_conditional_branch_count_internal::assign_directions(64, 50, 42);
  auto b = ferret::bias_conditional_branch_count_internal::assign_directions(64, 50, 42);
  EXPECT_EQ(a, b);
}

TEST(BiasConditionalBranchCount, DirectionAssignmentDiffersBetweenSeeds) {
  auto a = ferret::bias_conditional_branch_count_internal::assign_directions(64, 50, 42);
  auto b = ferret::bias_conditional_branch_count_internal::assign_directions(64, 50, 43);
  EXPECT_NE(a, b);
}

TEST(BiasConditionalBranchCount, DirectionAssignmentAllTPreferredWhenZero) {
  auto dirs = ferret::bias_conditional_branch_count_internal::assign_directions(128, 0, 1);
  for (uint8_t d : dirs) EXPECT_EQ(d, 0u);
}

TEST(BiasConditionalBranchCount, DirectionAssignmentAllNtPreferredWhenHundred) {
  auto dirs = ferret::bias_conditional_branch_count_internal::assign_directions(128, 100, 1);
  for (uint8_t d : dirs) EXPECT_EQ(d, 1u);
}
```

- [ ] **Step 2: Run to verify failure**

```bash
nix develop --command cmake --build build 2>&1 | tail -20
```

Expected: link error (`assign_directions` not defined).

- [ ] **Step 3: Implement `assign_directions`**

Edit `benchmarks/bias_conditional_branch_count.cpp`. Add `#include <algorithm>` and `#include <random>` at the top, then add an internal namespace before the struct:

```cpp
namespace ferret {

namespace bias_conditional_branch_count_internal {

constexpr uint64_t kDirBranchesMix = 0x9E3779B97F4A7C15ULL;
constexpr uint64_t kDirNtPctMix    = 0xD6E8FEB86659FD93ULL;
constexpr uint64_t kFillTotalMix   = 0xBF58476D1CE4E5B9ULL;
constexpr uint64_t kFillBiasMix    = 0x94D049BB133111EBULL;

std::vector<uint8_t> assign_directions(size_t branches, int64_t nt_branch_pct, uint64_t seed) {
  std::vector<uint8_t> dirs(branches, 0u);
  if (branches == 0) return dirs;
  size_t nt_count = static_cast<size_t>(static_cast<int64_t>(branches) * nt_branch_pct / 100);
  if (nt_count == 0) return dirs;
  if (nt_count >= branches) {
    std::fill(dirs.begin(), dirs.end(), uint8_t{1});
    return dirs;
  }
  // Seeded Fisher–Yates: pick `nt_count` distinct indices to flag NT-preferred.
  std::vector<size_t> idxs(branches);
  for (size_t i = 0; i < branches; ++i) idxs[i] = i;
  uint64_t mixed = seed ^ (static_cast<uint64_t>(branches) * kDirBranchesMix) ^
                   (static_cast<uint64_t>(nt_branch_pct) * kDirNtPctMix);
  std::mt19937_64 rng(mixed);
  for (size_t i = 0; i < nt_count; ++i) {
    std::uniform_int_distribution<size_t> dist(i, branches - 1);
    size_t j = dist(rng);
    std::swap(idxs[i], idxs[j]);
    dirs[idxs[i]] = 1u;
  }
  return dirs;
}

}  // namespace bias_conditional_branch_count_internal
```

(Leave the struct + macro registration as-is for now.)

- [ ] **Step 4: Run tests**

```bash
nix develop --command cmake --build build
nix develop --command ctest --test-dir build -R BiasConditionalBranchCount.DirectionAssignment --output-on-failure
```

Expected: all five direction-assignment tests pass.

- [ ] **Step 5: Write failing tests for `generate_pattern_fill`**

Append to `tests/test_bias_conditional_branch_count.cpp`:

```cpp
TEST(BiasConditionalBranchCount, FillIsZeroOrOnePerCell) {
  auto v = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(
      /*branches=*/8, /*pattern_period=*/32, /*bias_pct=*/95, /*nt_branch_pct=*/50, /*seed=*/7);
  ASSERT_EQ(v.size(), 256u);
  for (uint32_t x : v) {
    EXPECT_TRUE(x == 0u || x == 1u) << "out of {0,1}: " << x;
  }
}

TEST(BiasConditionalBranchCount, FillDeterministicForSameSeed) {
  auto a = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(8, 32, 95, 50, 7);
  auto b = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(8, 32, 95, 50, 7);
  EXPECT_EQ(a, b);
}

TEST(BiasConditionalBranchCount, FillDiffersBetweenSeeds) {
  auto a = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(8, 32, 95, 50, 7);
  auto b = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(8, 32, 95, 50, 8);
  EXPECT_NE(a, b);
}

TEST(BiasConditionalBranchCount, FillDiffersByBiasPct) {
  auto a = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(8, 256, 95, 50, 1);
  auto b = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(8, 256, 50, 50, 1);
  EXPECT_NE(a, b);
}

TEST(BiasConditionalBranchCount, FillFrequencyTracksBiasOnTPreferred) {
  // Spec §10.1 statistical-bounds test. T-preferred branch with bias_pct=95
  // over 1024 outcomes: empirical T-frequency must lie in [0.93, 0.97]
  // (~3-sigma bounds at p=0.95, n=1024). With nt_branch_pct=0 every branch
  // is T-preferred, so we can read any column.
  auto dirs = ferret::bias_conditional_branch_count_internal::assign_directions(64, 0, 11);
  for (uint8_t d : dirs) ASSERT_EQ(d, 0u);
  auto v = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(
      /*branches=*/64, /*pattern_period=*/1024, /*bias_pct=*/95, /*nt_branch_pct=*/0, /*seed=*/11);
  size_t ones = 0;
  for (size_t t = 0; t < 1024; ++t) ones += v[t * 64 + 0];
  double freq = static_cast<double>(ones) / 1024.0;
  EXPECT_GT(freq, 0.93);
  EXPECT_LT(freq, 0.97);
}

TEST(BiasConditionalBranchCount, FillFrequencyMirrorsBiasOnNtPreferred) {
  // nt_branch_pct=100 → every branch NT-preferred. With bias_pct=95, the
  // T-frequency should be ~5% (the *minority* direction).
  auto dirs = ferret::bias_conditional_branch_count_internal::assign_directions(64, 100, 11);
  for (uint8_t d : dirs) ASSERT_EQ(d, 1u);
  auto v = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(
      /*branches=*/64, /*pattern_period=*/1024, /*bias_pct=*/95, /*nt_branch_pct=*/100, /*seed=*/11);
  size_t ones = 0;
  for (size_t t = 0; t < 1024; ++t) ones += v[t * 64 + 0];
  double freq = static_cast<double>(ones) / 1024.0;
  EXPECT_GT(freq, 0.03);
  EXPECT_LT(freq, 0.07);
}
```

- [ ] **Step 6: Implement `generate_pattern_fill`**

Add inside `namespace bias_conditional_branch_count_internal` in `benchmarks/bias_conditional_branch_count.cpp`:

```cpp
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t pattern_period, int64_t bias_pct,
                                              int64_t nt_branch_pct, uint64_t seed) {
  std::vector<uint32_t> flat(branches * pattern_period, 0u);
  if (branches == 0 || pattern_period == 0) return flat;

  auto dirs = assign_directions(branches, nt_branch_pct, seed);

  // Fill RNG seed mix — depends on full param set but stays distinct from
  // the direction-assignment seed so direction stays stable when only
  // total_outcomes / bias_pct change.
  uint64_t dir_mix = seed ^ (static_cast<uint64_t>(branches) * kDirBranchesMix) ^
                     (static_cast<uint64_t>(nt_branch_pct) * kDirNtPctMix);
  uint64_t fill_mix = dir_mix ^ (static_cast<uint64_t>(pattern_period) * kFillTotalMix) ^
                      (static_cast<uint64_t>(bias_pct) * kFillBiasMix);
  std::mt19937_64 rng(fill_mix);

  // Fixed-point compare in Q14 (0..16384). r ∈ [0, 16384); branch taken iff
  // r < prob_taken_q14.
  auto prob_taken_q14 = [&](bool nt_preferred) -> uint32_t {
    int64_t pct = nt_preferred ? (100 - bias_pct) : bias_pct;
    return static_cast<uint32_t>(pct * 16384 / 100);
  };

  for (size_t t = 0; t < pattern_period; ++t) {
    for (size_t j = 0; j < branches; ++j) {
      uint32_t r = static_cast<uint32_t>(rng() & 0x3fffu);
      flat[t * branches + j] = (r < prob_taken_q14(dirs[j] != 0u)) ? 1u : 0u;
    }
  }
  return flat;
}
```

- [ ] **Step 7: Run tests**

```bash
nix develop --command cmake --build build
nix develop --command ctest --test-dir build -R BiasConditionalBranchCount.Fill --output-on-failure
```

Expected: all six fill tests pass.

- [ ] **Step 8: Commit**

```bash
git add benchmarks/bias_conditional_branch_count.cpp tests/test_bias_conditional_branch_count.cpp
git commit --no-gpg-sign -m "feat(bias_conditional_branch_count): direction assignment + outcome fill"
```

---

### Task 5: Wire `emit_kernel` to the shared chain emitter + validation

**Files:**
- Modify: `benchmarks/bias_conditional_branch_count.cpp`
- Modify: `tests/test_bias_conditional_branch_count.cpp`

- [ ] **Step 1: Write the failing emit/layout tests**

Append to `tests/test_bias_conditional_branch_count.cpp`:

```cpp
extern "C" {
#include <sljitLir.h>
}

namespace {
struct CompilerHandle {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  ~CompilerHandle() {
    if (c) sljit_free_compiler(c);
  }
};
}  // namespace

TEST(BiasConditionalBranchCount, RejectsZeroBranches) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(0, 8192)), std::invalid_argument);
}

TEST(BiasConditionalBranchCount, RejectsZeroTotalOutcomes) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 0)), std::invalid_argument);
}

TEST(BiasConditionalBranchCount, RejectsTotalOutcomesBelowBranches) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(64, 32)), std::invalid_argument);
}

TEST(BiasConditionalBranchCount, RejectsBiasPctOutOfRange) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 8192, /*bias=*/-1)), std::invalid_argument);
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 8192, /*bias=*/101)), std::invalid_argument);
}

TEST(BiasConditionalBranchCount, RejectsNtBranchPctOutOfRange) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 8192, 95, /*nt=*/-1)), std::invalid_argument);
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 8192, 95, /*nt=*/101)), std::invalid_argument);
}

TEST(BiasConditionalBranchCount, RejectsSpacingBytesTooSmall) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 8192, 95, 50, /*spacing=*/4)), std::invalid_argument);
}

TEST(BiasConditionalBranchCount, EmitsValidKernelForSmallParams) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*branches=*/4, /*total_outcomes=*/16, /*bias=*/95, /*nt=*/50, /*spacing=*/16);
  ASSERT_NO_THROW(b->emit_kernel(ch.c, p));
  ASSERT_EQ(sljit_get_compiler_error(ch.c), SLJIT_SUCCESS);

  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  ASSERT_NO_THROW(b->verify_layout(ch.c));
  sljit_free_code(code, nullptr);
}

TEST(BiasConditionalBranchCount, EmitsAcceptsEdgeCaseTotalOutcomesEqualsBranches) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  // total_outcomes == branches → pattern_period == 1. Spec §9: accepted.
  auto p = make_params(/*branches=*/4, /*total_outcomes=*/4);
  ASSERT_NO_THROW(b->emit_kernel(ch.c, p));
  ASSERT_EQ(sljit_get_compiler_error(ch.c), SLJIT_SUCCESS);
  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  sljit_free_code(code, nullptr);
}
```

- [ ] **Step 2: Replace the stub `emit_kernel` with the real implementation**

Edit `benchmarks/bias_conditional_branch_count.cpp`. Replace the `emit_kernel` body and add a `verify_layout` override. The full replacement for the struct + method definitions:

```cpp
struct BiasConditionalBranchCount : Benchmark {
  std::vector<uint32_t> flat_;
  BranchChainEmitResult last_emit_;
  size_t last_branches_ = 0;
  size_t last_spacing_ = 0;

  [[nodiscard]] std::string name() const override { return "bias_conditional_branch_count"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::geom_range("branches", 1, 1 << 13, /*samples_per_octave=*/1),
        Axis::geom_range("total_outcomes", 1 << 13, 1 << 20, /*samples_per_octave=*/1),
    };
  }

  [[nodiscard]] BenchOptions options() const override {
    return {
        BenchOption{.name = "bias_pct", .default_value = 95},
        BenchOption{.name = "nt_branch_pct", .default_value = 50},
        BenchOption{.name = "spacing_bytes", .default_value = 16},
    };
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("branches"); }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return std::max<size_t>(1, 10'000'000 / p.get<size_t>("branches"));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override;
  void verify_layout(sljit_compiler* c) override;
};

void BiasConditionalBranchCount::emit_kernel(sljit_compiler* c, const Params& p) {
  auto branches = p.get<size_t>("branches");
  auto total_outcomes = p.get<size_t>("total_outcomes");
  auto bias_pct = p.get<int64_t>("bias_pct");
  auto nt_branch_pct = p.get<int64_t>("nt_branch_pct");
  auto spacing = p.get<size_t>("spacing_bytes");
  auto seed = static_cast<uint64_t>(p.get<int64_t>("seed"));

  if (branches < 1) {
    throw std::invalid_argument("branches must be >= 1");
  }
  if (total_outcomes < 1) {
    throw std::invalid_argument("total_outcomes must be >= 1");
  }
  if (total_outcomes < branches) {
    throw std::invalid_argument("total_outcomes (" + std::to_string(total_outcomes) +
                                ") must be >= branches (" + std::to_string(branches) +
                                ") so pattern_period >= 1");
  }
  if (bias_pct < 0 || bias_pct > 100) {
    throw std::invalid_argument("bias_pct must be in [0, 100]; got " + std::to_string(bias_pct));
  }
  if (nt_branch_pct < 0 || nt_branch_pct > 100) {
    throw std::invalid_argument("nt_branch_pct must be in [0, 100]; got " + std::to_string(nt_branch_pct));
  }
  if (spacing < branch_chain_min_site_bytes()) {
    throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                " is smaller than the minimum site encoding (" +
                                std::to_string(branch_chain_min_site_bytes()) +
                                " bytes) on this architecture");
  }

  size_t pattern_period = total_outcomes / branches;
  size_t iters = iterations(p);

  flat_ = bias_conditional_branch_count_internal::generate_pattern_fill(
      branches, pattern_period, bias_pct, nt_branch_pct, seed);

  last_emit_ = emit_branch_chain(c, BranchChainEmitConfig{
                                        .flat_base = flat_.data(),
                                        .branches = branches,
                                        .pattern_period = pattern_period,
                                        .spacing_bytes = spacing,
                                        .iterations = iters,
                                    });
  last_branches_ = branches;
  last_spacing_ = spacing;
}

void BiasConditionalBranchCount::verify_layout(sljit_compiler* /*c*/) {
  if (last_branches_ == 0 || last_emit_.site_labels.empty()) {
    return;
  }
  verify_branch_chain_layout(last_emit_, last_branches_, last_spacing_);
}
```

Also add `#include <algorithm>` and `#include <random>` if not already present, and remove the `(void)c; (void)p; throw` stub.

- [ ] **Step 3: Build and run all `BiasConditionalBranchCount.*` tests**

```bash
nix develop --command cmake --build build
nix develop --command ctest --test-dir build -R BiasConditionalBranchCount --output-on-failure
```

Expected: all 20+ tests pass.

- [ ] **Step 4: Commit**

```bash
git add benchmarks/bias_conditional_branch_count.cpp tests/test_bias_conditional_branch_count.cpp
git commit --no-gpg-sign -m "feat(bias_conditional_branch_count): wire emit_kernel + validation"
```

---

### Task 6: Integration smoke test

**Files:**
- Modify: `tests/test_integration.cpp`

- [ ] **Step 1: Append the new integration tests**

The existing integration tests in `tests/test_integration.cpp` use `FERRET_BINARY` + `run(cmd)` + `slurp(path)` (no `make_tmp_csv` / `read_csv_rows` abstractions). Match that style exactly. Append the following block after the existing `Integration.BranchHistoryFootprint*` tests (around line 171):

```cpp
TEST(Integration, BiasConditionalBranchCountProducesExpectedRowCount) {
  auto out = std::filesystem::temp_directory_path() / "ferret_bcbc.csv";
  std::filesystem::remove(out);
  // branches ∈ {1, 2, 4} × total_outcomes ∈ {8, 16} → 6 data rows.
  std::string cmd = std::string(FERRET_BINARY) +
                    " run bias_conditional_branch_count"
                    " --branches=1..4 --total_outcomes=8..16"
                    " --bias_pct=95 --nt_branch_pct=50"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 7u);  // header + 6 data rows
  EXPECT_EQ(contents.find(",,\n"), std::string::npos);
}

TEST(Integration, BiasConditionalBranchCountHeaderHasExpectedColumns) {
  auto out = std::filesystem::temp_directory_path() / "ferret_bcbc_hdr.csv";
  std::filesystem::remove(out);
  std::string cmd = std::string(FERRET_BINARY) +
                    " run bias_conditional_branch_count"
                    " --branches=1 --total_outcomes=8"
                    " --reps=2 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  EXPECT_NE(contents.find("branches"), std::string::npos);
  EXPECT_NE(contents.find("total_outcomes"), std::string::npos);
  EXPECT_NE(contents.find("bias_pct"), std::string::npos);
  EXPECT_NE(contents.find("nt_branch_pct"), std::string::npos);
  EXPECT_NE(contents.find("spacing_bytes"), std::string::npos);
}

TEST(Integration, BiasConditionalBranchCountRejectsTotalOutcomesBelowBranches) {
  // Spec §9: total_outcomes < branches must fail the sweep cleanly.
  auto out = std::filesystem::temp_directory_path() / "ferret_bcbc_bad.csv";
  std::filesystem::remove(out);
  std::string cmd = std::string(FERRET_BINARY) +
                    " run bias_conditional_branch_count"
                    " --branches=64 --total_outcomes=8"
                    " --reps=1 --warmup=0"
                    " --out=" +
                    out.string();
  // measure_rows returns std::nullopt on invalid_argument; run() reports
  // non-zero exit.
  EXPECT_NE(0, run(cmd));
}
```

- [ ] **Step 2: Run integration tests**

```bash
nix develop --command cmake --build build
nix develop --command ctest --test-dir build -R Integration.BiasConditionalBranchCount --output-on-failure
```

Expected: all three tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/test_integration.cpp
git commit --no-gpg-sign -m "test(bias_conditional_branch_count): integration smoke + CSV schema"
```

---

### Task 7: Per-benchmark doc page

**Files:**
- Create: `docs/benchmarks/bias_conditional_branch_count.md`
- Modify: `README.md`

- [ ] **Step 1: Write the doc page**

Create `docs/benchmarks/bias_conditional_branch_count.md`:

```markdown
# `bias_conditional_branch_count` — SC bias-table capacity probe

`N` data-dependent conditional branches, half biased toward taken
(Bernoulli(p)) and half toward not-taken (Bernoulli(1-p)), each driven
by a long aperiodic outcome stream of total length `total_outcomes`
shared across all branches. Pattern period per branch =
`total_outcomes / branches`.

The benchmark is designed to expose the **SC (Statistical Corrector)
bias-table capacity** in TAGE-SC-L–style direction predictors by
holding tagged-table pressure roughly constant (set by
`total_outcomes`) while varying the number of distinct PCs (set by
`branches`). Mixed-direction biases produce destructive aliasing in
the SC bias table once `branches` exceeds the bias-table's effective
entry count.

## Kernel structure

Identical to `branch_history_footprint`: one outer loop, `N` sites
each loading a `uint32_t` outcome from the per-row buffer, comparing
against zero, and branching to the immediately-following instruction.
Architecturally a no-op; only the direction predictor is exercised.

```
   PC                  site (>= spacing_bytes apart)
 0x0000   ┌──────────────────────────────────────┐
          │  MOV  r2, [row_ptr + 0]              │
          │  CMP  r2, 0                          │
          │  JNE  .Lnext_0                       │ ──┐
          │ .Lnext_0:                            │ ◄─┘
          │  <NOP pad>                           │
 base+1×spacing+  ┌─────────────────────────────┐
          │  MOV  r2, [row_ptr + 4]              │
          │  CMP  r2, 0                          │
          │  JNE  .Lnext_1                       │ ──┐
          │ .Lnext_1:                            │ ◄─┘
          │  <NOP pad>                           │
 base+2×spacing+  ┌─────────────────────────────┐
          │   ...                                │
          ├──────────────────────────────────────┤
          │  ADD  hist_idx, hist_idx, 1          │
          │  CMP  hist_idx, pattern_period       │
          │  SUBS iters, iters, 1; B.NE loop_top │
          └──────────────────────────────────────┘
```

- The pattern buffer is `flat[pattern_period][branches]` (transposed).
- Each of the `branches` PCs is assigned a *preferred* direction (T or
  NT). At default `nt_branch_pct=50`, half prefer each.
- Outcomes are i.i.d. Bernoulli(`bias_pct/100`) toward the branch's
  preferred direction. Direction assignment is seed-deterministic and
  stable across `total_outcomes` values for a given `(branches,
  nt_branch_pct, seed)`.

## Per-benchmark options

| flag                   | meaning                                                  |
| ---------------------- | -------------------------------------------------------- |
| `--bias_pct=95`        | Per-branch bias magnitude (0..100). Default 95.          |
| `--nt_branch_pct=50`   | Percent of branches assigned NT-preferred. Default 50.   |
| `--spacing_bytes=16`   | Minimum PC stride per site (min 8 AArch64 / 6 x86_64).   |

## CLI surface

| flag                       | meaning                                                   |
| -------------------------- | --------------------------------------------------------- |
| `--branches=A..B[@k]`      | Geometric sweep, default `1..8192`.                       |
| `--total_outcomes=A..B[@k]`| Geometric sweep, default `8192..1048576`.                 |
| `--bias_pct=N`             | See above. Default 95.                                    |
| `--nt_branch_pct=N`        | See above. Default 50.                                    |
| `--spacing_bytes=N`        | Default 16.                                               |
| `--seed=…`                 | Seeds direction assignment + outcome fill.                |

`total_outcomes` must be `>= branches` for every Cartesian point
(`pattern_period >= 1`). Widen one and you may need to widen the
other.

See [`../cli.md`](../cli.md) for global flags.

## Reading the curves

Plot a surface with `branches` on one axis and `total_outcomes` on
the other:

```sh
python3 scripts/plot.py surface /tmp/sc.csv --out=/tmp/sc.png
```

Expected shape: an **L-shaped high-cycles region** in the upper-right
corner of the plane.

- **Lower-left plateau:** flat at the predictor-saturated floor.
  Tagged tables memorize the short pattern (low `total_outcomes`) or
  SC bias has room for every PC (low `branches`).
- **Cliff along `branches`:** vertical wall — the SC bias-table
  capacity. Below: SC corrects; above: destructive aliasing in SC
  slots pulls saturated counters toward zero and mispredict rate
  climbs.
- **Cliff along `total_outcomes`:** horizontal wall — the tagged-table
  effective memorization threshold. Below: tagged tables learn the
  cyclic pattern; above: they churn perpetually.
- **Upper-right plateau:** both predictors fail; cycles/site at the
  high-mispredict ceiling.

The SC fingerprint: the **vertical cliff position is invariant to
`total_outcomes`** (above the tagged-table threshold). If the cliff
slides diagonally with `total_outcomes`, the cliff is tagged-table-
pressure-dominated and the SC reading is confounded on this CPU.

## Caveats

- **Per-row L1d footprint at high `branches`.** With `uint32_t`
  outcomes, the per-row footprint is `branches * 4` bytes. At
  `branches = 8192` that's 32 KB — at or just past the L1d ceiling
  on most current cores. Cycles/site beyond that point may combine
  the SC-bias cliff with L1d-miss effects.
- **T0 may produce a small secondary cliff.** TAGE's base bimodal
  table is also PC-indexed; when `branches` exceeds T0 capacity, T0
  also aliases. SC bias normally corrects this, so the dominant
  cliff is SC's.
- **Other SC sub-components (GHIST/PATH/IMLI)** may partially correct
  what BIAS cannot. The aperiodic / history-independent design
  minimizes their contribution.
- **Outer-loop tax at `branches=1`.** Read the leftmost column as a
  tax baseline. Same caveat as `branch_history_footprint`.
- **Apple Silicon pinning.** See project README discipline section.

## Related docs

- Construction rationale: [design spec](../superpowers/specs/2026-05-20-bias-conditional-branch-count-design.md).
- Sister benchmark: [`branch_history_footprint`](branch_history_footprint.md) — direction-predictor capacity, generic.
- Project two-step workflow: [project README](../../README.md).
```

- [ ] **Step 2: Update README benchmark table**

Edit `README.md`. Find the benchmark table (around line 65). Add a row for the new benchmark:

```markdown
| [`bias_conditional_branch_count`](docs/benchmarks/bias_conditional_branch_count.md) | SC bias-table capacity (TAGE-SC-L) |
```

The new row goes after the `branch_history_footprint` row in the table.

- [ ] **Step 3: Verify both docs render and links resolve**

```bash
test -f docs/benchmarks/bias_conditional_branch_count.md && echo "doc OK"
grep -n "bias_conditional_branch_count" README.md
```

- [ ] **Step 4: Commit**

```bash
git add docs/benchmarks/bias_conditional_branch_count.md README.md
git commit --no-gpg-sign -m "docs(bias_conditional_branch_count): per-benchmark page + readme entry"
```

---

### Task 8: Full test + smoke run

- [ ] **Step 1: Run the complete test suite**

```bash
nix develop --command cmake --build build
nix develop --command ctest --test-dir build --output-on-failure
```

Expected: all tests pass (existing + new).

- [ ] **Step 2: Sanitizer build (ASan + UBSan)**

```bash
nix develop --command cmake -S . -B build-san -GNinja -DFERRET_SANITIZER=address+undefined
nix develop --command cmake --build build-san
nix develop --command ctest --test-dir build-san --output-on-failure
```

Expected: all tests pass under ASan + UBSan. Highest-value targets are the direction-assignment Fisher–Yates loop (off-by-one in the NT-set boundary) and the Q14 probability comparison (off-by-one at `bias_pct=0`/`100`).

- [ ] **Step 3: Smoke-run the benchmark from the command line**

```bash
nix develop --command ./build/ferret list | grep bias_conditional_branch_count
nix develop --command ./build/ferret run bias_conditional_branch_count \
    --branches=1..16 --total_outcomes=64..256 \
    --out=/tmp/sc-smoke.csv
head -5 /tmp/sc-smoke.csv
wc -l /tmp/sc-smoke.csv
```

Expected:
- `list` contains the new benchmark name.
- CSV has the expected columns (`branches`, `total_outcomes`, `bias_pct`, `nt_branch_pct`, `spacing_bytes`, ticks/iterations/sites/cycles/ns).
- 5 × 3 = 15 data rows plus a header (16 total lines).

- [ ] **Step 4: Optional: render the surface to confirm plot tooling works**

```bash
nix develop --command ./build/ferret run bias_conditional_branch_count \
    --branches=1..256 --total_outcomes=8192..65536 \
    --out=/tmp/sc-shape.csv
python3 scripts/plot.py surface /tmp/sc-shape.csv --out=/tmp/sc-shape.png
```

Expected: PNG renders without error. The qualitative shape interpretation is documented in §11 of the spec; full validation is the manual-platform-check step from the spec §10.4 and not part of this task.

- [ ] **Step 5: Confirm git status is clean**

```bash
git status
git log --oneline -8
```

Expected: clean tree; recent commits listed in this order (newest first):

```
docs(bias_conditional_branch_count): per-benchmark page + readme entry
test(bias_conditional_branch_count): integration smoke + CSV schema
feat(bias_conditional_branch_count): wire emit_kernel + validation
feat(bias_conditional_branch_count): direction assignment + outcome fill
test(bias_conditional_branch_count): lock in axes, options, sites/iterations
feat(bias_conditional_branch_count): scaffold benchmark + registry test
refactor(branch_chain_emit): extract shared sljit branch-chain emitter
docs(bias_conditional_branch_count): brainstorming design spec
```

If anything is missing or in the wrong order, surface to the user — do not amend silently.

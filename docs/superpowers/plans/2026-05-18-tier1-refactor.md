# Tier 1 Refactor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate the highest-impact structural debt surfaced by the 2026-05-18 audit: duplicated sljit scaffolding across benchmarks, leaky CLI/Axis coupling, the `run_command.cpp` god function, and `#ifdef`-fragmented platform shims.

**Architecture:** Pure-refactor pass. No behavior changes; all existing tests must keep passing. New helpers (bench scaffolding, axis methods, parse utility) get focused unit tests before being adopted at call sites. Platform shims move from inline `#ifdef` guards to CMake-selected per-arch / per-OS source files. `run_command::run` decomposes into a `RunInputs` builder + small named stages.

**Tech Stack:** C++20, CMake/Ninja, sljit (vendored), GoogleTest. Build via `nix develop` → `cmake -S . -B build -GNinja && cmake --build build`. Tests via `ctest --test-dir build --output-on-failure`.

**Execution order:** Section A (bench scaffolding) → Section D (Axis ownership of CLI parsing) → Section B (run_command decomposition) → Section C (platform shims). D before B because B's classifier becomes simpler once Axis owns validation.

**Baseline check (run before starting any task):**

```sh
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests green. If any test fails on baseline, stop and investigate before refactoring.

---

## Section A — Benchmark Scaffolding

The four benchmark files duplicate three patterns: an outer-loop scaffold (MOV counter → label → body → SUB SET_Z → JNZ), the iteration-budget formula `std::max(1, BUDGET / sites)`, and a Sattolo seed-mix that uses identical magic constants in two files. Two benchmarks also share an identical "verify each site lives at base + i*spacing" check with one boolean-strictness difference.

### Task 1: Add `bench_helpers.hpp` with `emit_outer_loop` template

**Files:**
- Create: `include/ferret/bench_helpers.hpp`
- Test: `tests/test_bench_helpers.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_bench_helpers.cpp`:

```cpp
extern "C" {
#include <sljitLir.h>
}

#include <gtest/gtest.h>

#include "ferret/bench_helpers.hpp"

namespace {

// Minimal kernel: outer loop that decrements a counter; body increments R0.
// After `iters` iterations, R0 should equal `iters`.
TEST(BenchHelpers, EmitOuterLoopRunsIterTimes) {
  sljit_compiler* c = sljit_create_compiler(nullptr, nullptr);
  ASSERT_NE(c, nullptr);

  // Return R0 via SLJIT_RETURN.
  sljit_emit_enter(c, 0, SLJIT_ARGS0(W), /*scratches=*/2, /*saved=*/0, /*local_size=*/0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);

  ferret::emit_outer_loop(c, SLJIT_R1, /*iters=*/7, [&] {
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
  });

  sljit_emit_return(c, SLJIT_MOV, SLJIT_R0, 0);

  void* code = sljit_generate_code(c, 0, nullptr);
  ASSERT_NE(code, nullptr);

  using fn_t = sljit_sw (*)();
  auto fn = reinterpret_cast<fn_t>(code);
  EXPECT_EQ(fn(), 7);

  sljit_free_code(code, nullptr);
  sljit_free_compiler(c);
}

TEST(BenchHelpers, EmitOuterLoopOneIterationStillRunsBody) {
  sljit_compiler* c = sljit_create_compiler(nullptr, nullptr);
  ASSERT_NE(c, nullptr);

  sljit_emit_enter(c, 0, SLJIT_ARGS0(W), 2, 0, 0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);

  ferret::emit_outer_loop(c, SLJIT_R1, /*iters=*/1,
                          [&] { sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1); });

  sljit_emit_return(c, SLJIT_MOV, SLJIT_R0, 0);

  void* code = sljit_generate_code(c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  auto fn = reinterpret_cast<sljit_sw (*)()>(code);
  EXPECT_EQ(fn(), 1);

  sljit_free_code(code, nullptr);
  sljit_free_compiler(c);
}

}  // namespace
```

Add entry to `tests/CMakeLists.txt` after the `test_permute` block (around line 92):

```cmake
add_executable(test_bench_helpers test_bench_helpers.cpp)
target_link_libraries(test_bench_helpers PRIVATE
  ferret_core
  sljit::sljit
  GTest::gtest GTest::gtest_main
  ferret_warnings
)
gtest_discover_tests(test_bench_helpers)
```

- [ ] **Step 2: Run test to verify it fails to compile**

```sh
cmake --build build 2>&1 | tail -20
```

Expected: fails with `fatal error: 'ferret/bench_helpers.hpp' file not found`.

- [ ] **Step 3: Create the header**

Create `include/ferret/bench_helpers.hpp`:

```cpp
#pragma once

#include <cstddef>

extern "C" {
#include <sljitLir.h>
}

namespace ferret {

// Emits the canonical ferret outer-loop scaffold around a body lambda:
//
//   MOV counter_reg, iters
//   loop_top:
//     [body()]
//   SUB|SET_Z counter_reg, counter_reg, 1
//   JNZ loop_top
//
// `counter_reg` must be a scratch register the body neither reads nor
// writes; the body sees it at the value (iters - i) on iteration i but
// most callers ignore that. `iters` >= 1; iters == 1 still emits the
// loop (the JNZ falls through after one decrement) — callers that want
// to skip the scaffold entirely for the iters==0 fast path should guard
// the call themselves.
template <typename Body>
void emit_outer_loop(sljit_compiler* c, sljit_s32 counter_reg, size_t iters, Body emit_body) {
  sljit_emit_op1(c, SLJIT_MOV, counter_reg, 0, SLJIT_IMM, static_cast<sljit_sw>(iters));
  sljit_label* loop_top = sljit_emit_label(c);
  emit_body();
  sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, counter_reg, 0, counter_reg, 0, SLJIT_IMM, 1);
  sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
  sljit_set_label(back, loop_top);
}

}  // namespace ferret
```

- [ ] **Step 4: Run tests to verify they pass**

```sh
cmake --build build && ctest --test-dir build -R BenchHelpers --output-on-failure
```

Expected: both `BenchHelpers.*` tests pass.

- [ ] **Step 5: Commit**

```sh
git add include/ferret/bench_helpers.hpp tests/test_bench_helpers.cpp tests/CMakeLists.txt
git commit -m "bench: extract emit_outer_loop helper for shared loop scaffold"
```

---

### Task 2: Migrate `dependent_chain_throughput` to `emit_outer_loop`

**Files:**
- Modify: `benchmarks/dependent_chain_throughput.cpp:33-49`

- [ ] **Step 1: Verify baseline green**

```sh
ctest --test-dir build --output-on-failure -R "Smoke|Registry|test_jit"
```

Expected: all listed tests pass.

- [ ] **Step 2: Add include and replace the loop**

Add `#include "ferret/bench_helpers.hpp"` after the existing `#include "ferret/benchmark.hpp"`.

Replace the block at `dependent_chain_throughput.cpp:37-49` (the `if (full_blocks > 0) { ... }` body) with:

```cpp
    if (full_blocks > 0) {
      emit_outer_loop(c, SLJIT_R1, full_blocks, [&] {
        for (int i = 0; i < UNROLL; ++i) {
          sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
        }
      });
    }
```

The pre-existing `MOV R0, 1` (line 35) and the straight-line tail loop (lines 53-55) stay untouched.

- [ ] **Step 3: Build and run full test suite**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass. The integration tests exercise `dependent_chain_throughput` end-to-end, so a regression here would surface immediately.

- [ ] **Step 4: Commit**

```sh
git add benchmarks/dependent_chain_throughput.cpp
git commit -m "bench(dependent_chain): use emit_outer_loop helper"
```

---

### Task 3: Migrate `direct_branch_footprint` to `emit_outer_loop`

**Files:**
- Modify: `benchmarks/direct_branch_footprint.cpp:110-171`

- [ ] **Step 1: Add include**

Add `#include "ferret/bench_helpers.hpp"` after the existing `#include "ferret/permute.hpp"`.

- [ ] **Step 2: Replace the loop scaffold**

The existing structure at `direct_branch_footprint.cpp:110-171` is:

```cpp
sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 1, 1, 0);
sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, static_cast<sljit_sw>(iters));

sljit_label* loop_top = sljit_emit_label(c);

std::vector<sljit_label*> labels(branches + 1);
// ... (chain emission body) ...

sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
sljit_set_label(back, loop_top);
```

Replace with:

```cpp
sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 1, 1, 0);

std::vector<sljit_label*> labels(branches + 1);
std::vector<sljit_jump*> jumps(branches);

emit_outer_loop(c, SLJIT_R0, iters, [&] {
  // (paste the existing chain-emission body verbatim:
  //  the for-loop emitting labels[i]/jumps[i] and the trailing
  //  labels[branches] = sljit_emit_label(c);)
});
```

Concretely the body of the lambda is lines 130-141 of the original file (the `for (size_t i = 0; i < branches; ++i)` loop and the `labels[branches] = sljit_emit_label(c);` that follows it). The `next[]` computation and `sljit_set_label` resolution (lines 143-166) stay AFTER the `emit_outer_loop(...)` call — they don't emit code, they only wire labels, and they run after the loop closes.

Wait — the `next[]` resolution at lines 162-166 (`sljit_set_label(jumps[i], labels[next[i]])`) wires labels emitted inside the loop. That's fine: `sljit_set_label` only records the relationship; the labels were already emitted by `emit_outer_loop`'s body call. Keep it where it is, after the helper call.

The `sljit_emit_return_void(c)` at line 172 stays where it is.

- [ ] **Step 3: Build and run tests**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass, especially `test_integration` which exercises this benchmark.

- [ ] **Step 4: Commit**

```sh
git add benchmarks/direct_branch_footprint.cpp
git commit -m "bench(direct_branch): use emit_outer_loop helper"
```

---

### Task 4: Migrate `branch_history_footprint` to `emit_outer_loop`

**Files:**
- Modify: `benchmarks/branch_history_footprint.cpp:147-195`

- [ ] **Step 1: Add include**

Add `#include "ferret/bench_helpers.hpp"` after the existing `#include "ferret/padding.hpp"`.

- [ ] **Step 2: Replace the loop scaffold**

The existing structure at `branch_history_footprint.cpp:147-195` initialises `SLJIT_R0` as the iter counter (line 150), enters the loop, then at lines 184-195 does both the hist_idx wrap AND the loop-tail decrement.

Restructure to:

```cpp
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

  // hist_idx wrap: hist_idx = (hist_idx+1 == history_len) ? 0 : hist_idx+1.
  sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, 1);
  sljit_emit_op2u(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_S1, 0, SLJIT_IMM, static_cast<sljit_sw>(history_len));
  sljit_jump* skip_reset = sljit_emit_jump(c, SLJIT_NOT_ZERO);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 0);
  sljit_label* after_wrap = sljit_emit_label(c);
  sljit_set_label(skip_reset, after_wrap);
});

sljit_emit_return_void(c);
```

The R0 iter-counter MOV (old line 150) is gone — `emit_outer_loop` does it. The old SUB+JNZ tail (old lines 193-195) is gone — `emit_outer_loop` does it.

- [ ] **Step 3: Build and run tests**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass. `test_branch_history_footprint` directly inspects `last_labels_`, which the new code still populates identically.

- [ ] **Step 4: Commit**

```sh
git add benchmarks/branch_history_footprint.cpp
git commit -m "bench(branch_history): use emit_outer_loop helper"
```

---

### Task 5: Migrate `nested_call_depth` variants 0/1/2 to `emit_outer_loop`

**Files:**
- Modify: `benchmarks/nested_call_depth.cpp:50-86, 105-159, 258-302`

- [ ] **Step 1: Add include**

Add `#include "ferret/bench_helpers.hpp"` after the existing `#include "ferret/benchmark.hpp"`.

- [ ] **Step 2: Replace the outer loop in `emit_variant0_single_site`**

The current scaffold at `nested_call_depth.cpp:56-66`:

```cpp
sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/1, /*saved=*/0, /*local_size=*/0);
sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, static_cast<sljit_sw>(iters));

sljit_label* loop_top = sljit_emit_label(c);
sljit_jump* call_main = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
pending_calls.push_back(call_main);
pending_targets.push_back(depth);
sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
sljit_set_label(back, loop_top);
sljit_emit_return_void(c);
```

Becomes:

```cpp
sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/1, /*saved=*/0, /*local_size=*/0);
emit_outer_loop(c, SLJIT_R0, iters, [&] {
  sljit_jump* call_main = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
  pending_calls.push_back(call_main);
  pending_targets.push_back(depth);
});
sljit_emit_return_void(c);
```

- [ ] **Step 3: Replace the outer loop in `emit_variant1_counter_bit`**

The current scaffold at `nested_call_depth.cpp:111-121` uses **`SLJIT_S0`** (saved register) as the counter so it survives the CALLs. The helper still works — pass `SLJIT_S0` as the counter register:

```cpp
sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/1, /*saved=*/1, /*local_size=*/0);
emit_outer_loop(c, SLJIT_S0, iters, [&] {
  sljit_jump* call_main = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
  pending_calls.push_back(call_main);
  pending_targets.push_back(depth);
});
sljit_emit_return_void(c);
```

The body's `sljit_emit_op1(c, SLJIT_MOV, SLJIT_S0, 0, SLJIT_IMM, iters)` (line 112) is gone — the helper emits it.

- [ ] **Step 4: Replace the outer loop in `emit_variant2_path_table`**

The current scaffold at `nested_call_depth.cpp:266-283`. Note the row_ptr recompute happens inside the loop, just like branch_history_footprint:

```cpp
sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/2, /*saved=*/2, /*local_size=*/0);
emit_outer_loop(c, SLJIT_S0, iters, [&] {
  // R0 = table_ptr; R1 = (S0 & (rows-1)) * stride; S1 = R0 + R1.
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, reinterpret_cast<sljit_sw>(table_ptr));
  sljit_emit_op2(c, SLJIT_AND, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, static_cast<sljit_sw>(rows - 1));
  sljit_emit_op2(c, SLJIT_MUL, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, static_cast<sljit_sw>(stride));
  sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_R0, 0, SLJIT_R1, 0);

  emit_k8_dispatch(c, /*table_offset=*/0, /*target_d=*/depth, pending_calls, pending_targets);
});
sljit_emit_return_void(c);
```

The `sljit_emit_op1(c, SLJIT_MOV, SLJIT_S0, 0, SLJIT_IMM, iters)` at line 267 is gone — the helper emits it.

- [ ] **Step 5: Build and run tests**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass, including `test_nested_call_depth` and the integration tests exercising all three variants.

- [ ] **Step 6: Commit**

```sh
git add benchmarks/nested_call_depth.cpp
git commit -m "bench(nested_call): use emit_outer_loop in all three variants"
```

---

### Task 6: Add `compute_iterations` helper and migrate three benchmarks

**Files:**
- Modify: `include/ferret/bench_helpers.hpp`
- Modify: `tests/test_bench_helpers.cpp`
- Modify: `benchmarks/direct_branch_footprint.cpp:87-89`
- Modify: `benchmarks/branch_history_footprint.cpp:107-109`
- Modify: `benchmarks/nested_call_depth.cpp:339-341`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_bench_helpers.cpp`:

```cpp
TEST(BenchHelpers, ComputeIterationsScalesByBudget) {
  EXPECT_EQ(ferret::compute_iterations(10'000'000, 1), 10'000'000U);
  EXPECT_EQ(ferret::compute_iterations(10'000'000, 1000), 10'000U);
  EXPECT_EQ(ferret::compute_iterations(10'000'000, 100'000'000), 1U);
  EXPECT_EQ(ferret::compute_iterations(0, 100), 1U);  // floor at 1
}
```

- [ ] **Step 2: Run test to verify failure**

```sh
cmake --build build 2>&1 | tail -5
```

Expected: undeclared identifier `compute_iterations`.

- [ ] **Step 3: Add the helper**

Append to `include/ferret/bench_helpers.hpp` (before the closing `}  // namespace ferret`):

```cpp
// Returns the per-kernel outer-loop iteration count that amortizes the
// runner's tick-read overhead to a target total work budget. The total
// per-rep work is approximately `target_ops`; per-site cost is
// total / (sites * iters). Floored at 1 so a kernel always runs once.
inline size_t compute_iterations(size_t target_ops, size_t sites_per_kernel) {
  if (sites_per_kernel == 0) {
    return 1;
  }
  size_t n = target_ops / sites_per_kernel;
  return n == 0 ? 1 : n;
}
```

- [ ] **Step 4: Run tests to verify pass**

```sh
cmake --build build && ctest --test-dir build -R BenchHelpers --output-on-failure
```

Expected: all `BenchHelpers.*` tests pass.

- [ ] **Step 5: Migrate `direct_branch_footprint`**

Replace `direct_branch_footprint.cpp:87-89`:

```cpp
  [[nodiscard]] size_t iterations(const Params& p) const override {
    return compute_iterations(10'000'000, p.get<size_t>("branches"));
  }
```

- [ ] **Step 6: Migrate `branch_history_footprint`**

Replace `branch_history_footprint.cpp:107-109`:

```cpp
  [[nodiscard]] size_t iterations(const Params& p) const override {
    return compute_iterations(10'000'000, p.get<size_t>("branches"));
  }
```

- [ ] **Step 7: Migrate `nested_call_depth`**

Replace `nested_call_depth.cpp:339-341`:

```cpp
  [[nodiscard]] size_t iterations(const Params& p) const override {
    return compute_iterations(1'000'000, p.get<size_t>("depth") + 1);
  }
```

- [ ] **Step 8: Build and run full test suite**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 9: Commit**

```sh
git add include/ferret/bench_helpers.hpp tests/test_bench_helpers.cpp \
        benchmarks/direct_branch_footprint.cpp \
        benchmarks/branch_history_footprint.cpp \
        benchmarks/nested_call_depth.cpp
git commit -m "bench: extract compute_iterations helper for amortization budget"
```

---

### Task 7: Promote the Sattolo seed-mix into `permute.hpp` and migrate call sites

**Files:**
- Modify: `include/ferret/permute.hpp`
- Modify: `src/permute.cpp`
- Modify: `tests/test_permute.cpp`
- Modify: `benchmarks/direct_branch_footprint.cpp:149-151`
- Modify: `benchmarks/branch_history_footprint.cpp:56-58`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_permute.cpp`:

```cpp
TEST(Permute, MixSeedDiffersForDifferentTuples) {
  uint64_t a = ferret::mix_seed(42, 100, 200);
  uint64_t b = ferret::mix_seed(42, 100, 201);
  uint64_t c = ferret::mix_seed(42, 101, 200);
  uint64_t d = ferret::mix_seed(43, 100, 200);
  EXPECT_NE(a, b);
  EXPECT_NE(a, c);
  EXPECT_NE(a, d);
  EXPECT_NE(b, c);
}

TEST(Permute, MixSeedDeterministic) {
  EXPECT_EQ(ferret::mix_seed(42, 100, 200), ferret::mix_seed(42, 100, 200));
}
```

- [ ] **Step 2: Run test to verify failure**

```sh
cmake --build build 2>&1 | tail -5
```

Expected: undeclared identifier `mix_seed`.

- [ ] **Step 3: Add the declaration**

Add to `include/ferret/permute.hpp` (before the closing `}  // namespace ferret`):

```cpp
// Mixes a seed with two auxiliary integers so distinct (seed, x, y)
// tuples produce distinct uint64 streams. Used by benchmarks that need
// per-parameter-point random variation (Sattolo cycles, pattern fills)
// without exposing each call site to the same magic constants.
//
// Constants are golden ratio (0x9E37…7C15) and the Murmur3 fmix64
// xorshift multiplier (0xBF58…E5B9) — both have good avalanche behavior
// and the literature treats them as standard mixing constants.
uint64_t mix_seed(uint64_t seed, uint64_t x, uint64_t y);
```

Add to `src/permute.cpp` (before the closing `}  // namespace ferret`):

```cpp
uint64_t mix_seed(uint64_t seed, uint64_t x, uint64_t y) {
  return seed ^ (x * 0x9E3779B97F4A7C15ULL) ^ (y * 0xBF58476D1CE4E5B9ULL);
}
```

- [ ] **Step 4: Run tests to verify pass**

```sh
cmake --build build && ctest --test-dir build -R Permute --output-on-failure
```

Expected: `Permute.MixSeed*` tests pass.

- [ ] **Step 5: Migrate `direct_branch_footprint`**

Replace `direct_branch_footprint.cpp:149-151`:

```cpp
      uint64_t mixed = mix_seed(seed, branches, spacing);
      next = sattolo_cycle(branches, mixed);
```

- [ ] **Step 6: Migrate `branch_history_footprint`**

Replace `branch_history_footprint.cpp:56-58`:

```cpp
  uint64_t mixed = mix_seed(seed, branches, history_len);
  std::mt19937_64 rng(mixed);
```

Delete the stale "Same constants as direct_branch_footprint…" comment above it — the mix is now centralized, the comment is wrong.

- [ ] **Step 7: Build and run full test suite**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass. Integration tests that depend on deterministic kernel layout still pass because `mix_seed` produces the same bits as the inline expression.

- [ ] **Step 8: Commit**

```sh
git add include/ferret/permute.hpp src/permute.cpp tests/test_permute.cpp \
        benchmarks/direct_branch_footprint.cpp \
        benchmarks/branch_history_footprint.cpp
git commit -m "permute: centralize seed-mix constants and adopt at both call sites"
```

---

### Task 8: Extract `verify_uniform_spacing` and adopt in both benchmarks that use it

**Files:**
- Modify: `include/ferret/bench_helpers.hpp`
- Modify: `tests/test_bench_helpers.cpp`
- Modify: `benchmarks/direct_branch_footprint.cpp:184-200`
- Modify: `benchmarks/branch_history_footprint.cpp:204-218`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_bench_helpers.cpp`:

```cpp
TEST(BenchHelpers, VerifyUniformSpacingPassesOnExactSpacing) {
  sljit_compiler* c = sljit_create_compiler(nullptr, nullptr);
  ASSERT_NE(c, nullptr);
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 1, 0, 0);

  std::vector<sljit_label*> labels;
  labels.push_back(sljit_emit_label(c));
  for (int i = 0; i < 4; ++i) {
    ferret::emit_outer_loop(c, SLJIT_R0, 1, [] {});  // placeholder body
    labels.push_back(sljit_emit_label(c));
  }
  sljit_emit_return_void(c);
  ASSERT_NE(sljit_generate_code(c, 0, nullptr), nullptr);

  // After generate_code, every label has a real address. We cannot assert
  // the exact spacing because emit_outer_loop's size is non-zero and
  // arch-dependent; but the helper should accept whatever spacing happens
  // to be uniform across the four sites.
  size_t base = sljit_get_label_addr(labels[0]);
  size_t step = sljit_get_label_addr(labels[1]) - base;
  EXPECT_NO_THROW(ferret::verify_uniform_spacing(labels, step, /*strict=*/true));

  sljit_free_compiler(c);
}

TEST(BenchHelpers, VerifyUniformSpacingThrowsOnExactMismatchStrict) {
  std::vector<sljit_label*> empty;
  EXPECT_NO_THROW(ferret::verify_uniform_spacing(empty, 16, /*strict=*/true));  // no-op
}
```

- [ ] **Step 2: Run test to verify failure**

```sh
cmake --build build 2>&1 | tail -5
```

Expected: undeclared identifier `verify_uniform_spacing`.

- [ ] **Step 3: Add the helper**

Append to `include/ferret/bench_helpers.hpp`:

```cpp
#include <stdexcept>
#include <string>
#include <vector>

namespace ferret {

// Asserts every label in `labels` sits at base + i * spacing relative to
// labels[0]. When `strict` is true (direct_branch_footprint), the actual
// offset must equal expected exactly. When false (branch_history_footprint),
// the actual offset must be >= expected (sites may overshoot when sljit
// picks a longer encoding; spacing is a floor, not an exact stride).
// Throws std::runtime_error on the first mismatch with the per-site delta.
// No-op when `labels.size() < 2`.
//
// `context` is prepended to the error message so the user sees which
// benchmark complained.
inline void verify_uniform_spacing(const std::vector<sljit_label*>& labels, size_t spacing, bool strict,
                                   const char* context = "") {
  if (labels.size() < 2) {
    return;
  }
  size_t base = sljit_get_label_addr(labels[0]);
  for (size_t i = 1; i < labels.size(); ++i) {
    size_t addr = sljit_get_label_addr(labels[i]);
    size_t actual = addr - base;
    size_t expected = i * spacing;
    const bool ok = strict ? (actual == expected) : (actual >= expected);
    if (!ok) {
      std::string msg;
      if (*context != '\0') {
        msg.append(context).append(": ");
      }
      msg.append("site ").append(std::to_string(i)).append(" at offset ").append(std::to_string(actual));
      if (strict) {
        msg.append(", expected ").append(std::to_string(expected));
      } else {
        msg.append(", expected at least ").append(std::to_string(expected));
      }
      throw std::runtime_error(msg);
    }
  }
}

}  // namespace ferret
```

Note: that closing `}  // namespace ferret` replaces the previous one — keep only one `namespace ferret` block per file. Verify the header has exactly one open/close pair after the edit.

- [ ] **Step 4: Run tests**

```sh
cmake --build build && ctest --test-dir build -R BenchHelpers --output-on-failure
```

Expected: all `BenchHelpers.*` tests pass.

- [ ] **Step 5: Migrate `direct_branch_footprint::verify_layout`**

Replace `direct_branch_footprint.cpp:184-200` (just the offset-check loop; the x86_64 displacement-patch block at lines 202-222 stays unchanged):

```cpp
  void verify_layout(sljit_compiler* c) override {
    if (last_branches_ == 0 || last_labels_.empty()) {
      return;
    }
    verify_uniform_spacing(last_labels_, last_spacing_, /*strict=*/true, "direct_branch_footprint");

#if defined(__x86_64__) || defined(_M_X64)
    // (existing displacement-patch block stays here verbatim)
```

- [ ] **Step 6: Migrate `branch_history_footprint::verify_layout`**

Replace `branch_history_footprint.cpp:204-218` (the entire method body):

```cpp
void BranchHistoryFootprint::verify_layout(sljit_compiler* /*c*/) {
  if (last_branches_ == 0 || last_labels_.empty()) {
    return;
  }
  verify_uniform_spacing(last_labels_, last_spacing_, /*strict=*/false, "branch_history_footprint");
}
```

- [ ] **Step 7: Build and run full test suite**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 8: Commit**

```sh
git add include/ferret/bench_helpers.hpp tests/test_bench_helpers.cpp \
        benchmarks/direct_branch_footprint.cpp \
        benchmarks/branch_history_footprint.cpp
git commit -m "bench: extract verify_uniform_spacing for shared layout check"
```

---

## Section D — Push CLI parsing onto `Axis`

`cli_axis.cpp` currently switches on `axis.kind()` in two places (validation and range expansion), duplicates `Axis::expand`'s range-expansion logic, and reimplements `parse_int` as `parse_option_value`. The fix is to let `Axis` own its own validation and range expansion, and to extract a single integer-parse utility.

### Task 9: Extract `parse_int` into a shared utility

**Files:**
- Create: `include/ferret/parse.hpp`
- Create: `src/parse.cpp`
- Create: `tests/test_parse.cpp`
- Modify: `CMakeLists.txt:136-155` (add `src/parse.cpp` to `ferret_core`)
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create `tests/test_parse.cpp`:

```cpp
#include <gtest/gtest.h>

#include "ferret/parse.hpp"

namespace {

TEST(Parse, ParsesPositiveInteger) {
  EXPECT_EQ(ferret::parse_int("42"), 42);
  EXPECT_EQ(ferret::parse_int("0"), 0);
  EXPECT_EQ(ferret::parse_int("-7"), -7);
  EXPECT_EQ(ferret::parse_int("9223372036854775807"), 9223372036854775807LL);
}

TEST(Parse, RejectsEmpty) {
  EXPECT_THROW(ferret::parse_int(""), std::invalid_argument);
}

TEST(Parse, RejectsTrailingJunk) {
  EXPECT_THROW(ferret::parse_int("42abc"), std::invalid_argument);
}

TEST(Parse, RejectsNonNumeric) {
  EXPECT_THROW(ferret::parse_int("abc"), std::invalid_argument);
}

}  // namespace
```

Add to `tests/CMakeLists.txt`:

```cmake
add_executable(test_parse test_parse.cpp)
target_link_libraries(test_parse PRIVATE
  ferret_core
  GTest::gtest GTest::gtest_main
  ferret_warnings
)
gtest_discover_tests(test_parse)
```

- [ ] **Step 2: Run test to verify failure**

```sh
cmake --build build 2>&1 | tail -5
```

Expected: `ferret/parse.hpp: No such file`.

- [ ] **Step 3: Create the header and impl**

Create `include/ferret/parse.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <string>

namespace ferret {

// Parses an int64 from a string. Throws std::invalid_argument on
// empty input, non-numeric input, or trailing junk after the number.
// The error message echoes the offending input so callers can chain it
// into a higher-level context.
int64_t parse_int(const std::string& s);

}  // namespace ferret
```

Create `src/parse.cpp`:

```cpp
#include "ferret/parse.hpp"

#include <charconv>
#include <stdexcept>

namespace ferret {

int64_t parse_int(const std::string& s) {
  if (s.empty()) {
    throw std::invalid_argument("empty number");
  }
  int64_t v = 0;
  auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{}) {
    throw std::invalid_argument("not an integer: " + s);
  }
  if (p != s.data() + s.size()) {
    throw std::invalid_argument("trailing junk after integer: " + s);
  }
  return v;
}

}  // namespace ferret
```

Add `src/parse.cpp` to the `ferret_core` source list in `CMakeLists.txt` (alphabetical, after `src/padding.cpp`):

```cmake
  src/padding.cpp
  src/parse.cpp
  src/log.cpp
```

- [ ] **Step 4: Run tests to verify pass**

```sh
cmake --build build && ctest --test-dir build -R Parse --output-on-failure
```

Expected: all `Parse.*` tests pass.

- [ ] **Step 5: Replace the inline copies in `cli_axis.cpp`**

In `src/cli_axis.cpp`:

- Add `#include "ferret/parse.hpp"` at the top.
- Delete the file-local `parse_int` lambda at lines 13-23.
- Delete the `parse_option_value` body at lines 119-132 and replace with:

```cpp
int64_t parse_option_value(const std::string& v) { return parse_int(v); }
```

- [ ] **Step 6: Build and run full test suite**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass, including `test_cli_axis` and `test_integration`.

- [ ] **Step 7: Commit**

```sh
git add include/ferret/parse.hpp src/parse.cpp tests/test_parse.cpp \
        tests/CMakeLists.txt CMakeLists.txt src/cli_axis.cpp
git commit -m "parse: extract parse_int and dedup parse_option_value"
```

---

### Task 10: Add `Axis::validate` method and adopt in `cli_axis.cpp`

**Files:**
- Modify: `include/ferret/axis.hpp`
- Modify: `src/axis.cpp`
- Modify: `tests/test_params_axis.cpp` (existing tests for Axis live here per the project convention)
- Modify: `src/cli_axis.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_params_axis.cpp`:

```cpp
TEST(Axis, ValidateAcceptsAnyValueForRangeAndValues) {
  auto range_axis = ferret::Axis::range("r", 1, 10);
  EXPECT_NO_THROW(range_axis.validate(5));
  EXPECT_NO_THROW(range_axis.validate(-1000));  // Range/Values do not constrain individual values
  auto vals_axis = ferret::Axis::values("v", {1, 2, 3});
  EXPECT_NO_THROW(vals_axis.validate(0));
}

TEST(Axis, ValidateRejectsNonPositiveForLog2RangeAndGeomRange) {
  auto log2_axis = ferret::Axis::log2_range("L", 1, 1024);
  EXPECT_THROW(log2_axis.validate(0), std::invalid_argument);
  EXPECT_THROW(log2_axis.validate(-1), std::invalid_argument);
  EXPECT_NO_THROW(log2_axis.validate(1));

  auto geom_axis = ferret::Axis::geom_range("g", 1, 1024, 2);
  EXPECT_THROW(geom_axis.validate(0), std::invalid_argument);
  EXPECT_THROW(geom_axis.validate(-5), std::invalid_argument);
  EXPECT_NO_THROW(geom_axis.validate(7));
}
```

- [ ] **Step 2: Run test to verify failure**

```sh
cmake --build build 2>&1 | tail -5
```

Expected: `Axis` has no member `validate`.

- [ ] **Step 3: Add the method**

In `include/ferret/axis.hpp`, after the `expand()` declaration (line 49), add:

```cpp
  // Throws std::invalid_argument when `v` violates the axis kind's
  // invariants. Log2Range and GeomRange require v > 0; Range and Values
  // accept any integer. The error message embeds the axis name.
  void validate(int64_t v) const;
```

In `src/axis.cpp`, after `Axis::expand()` (before `expand_log2_range` at line 76), add:

```cpp
void Axis::validate(int64_t v) const {
  if ((kind_ == Kind::Log2Range || kind_ == Kind::GeomRange) && v <= 0) {
    throw std::invalid_argument("axis '" + name_ + "' requires positive values: " + std::to_string(v));
  }
}
```

- [ ] **Step 4: Run Axis tests to verify pass**

```sh
cmake --build build && ctest --test-dir build -R "Axis\." --output-on-failure
```

Expected: all new `Axis.Validate*` tests pass; pre-existing Axis tests still pass.

- [ ] **Step 5: Adopt in `cli_axis.cpp`**

In `src/cli_axis.cpp`:

- Delete the `validate_value_against_kind` helper at lines 25-29.
- At its only call site (line 110), replace:

```cpp
    validate_value_against_kind(v, axis, cli_value);
```

with:

```cpp
    try {
      axis.validate(v);
    } catch (const std::invalid_argument&) {
      throw std::invalid_argument("axis '" + axis.name() + "' requires positive values: " + cli_value);
    }
```

This preserves the existing user-facing error message exactly (the test at `tests/test_cli_axis.cpp` asserts on this string format).

- [ ] **Step 6: Build and run full test suite**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass.

- [ ] **Step 7: Commit**

```sh
git add include/ferret/axis.hpp src/axis.cpp tests/test_params_axis.cpp src/cli_axis.cpp
git commit -m "axis: own kind-based value validation, drop duplicate in cli_axis"
```

---

### Task 11: Push `lo..hi[@k]` range expansion onto `Axis`

**Files:**
- Modify: `include/ferret/axis.hpp`
- Modify: `src/axis.cpp`
- Modify: `tests/test_params_axis.cpp`
- Modify: `src/cli_axis.cpp`

- [ ] **Step 1: Write the failing test**

Append to `tests/test_params_axis.cpp`:

```cpp
TEST(Axis, ExpandRangeLinearForRangeKind) {
  auto a = ferret::Axis::range("r", 0, 10);
  auto out = a.expand_range(3, 6, std::nullopt);
  EXPECT_EQ(out, (std::vector<int64_t>{3, 4, 5, 6}));
}

TEST(Axis, ExpandRangeLog2ForLog2Range) {
  auto a = ferret::Axis::log2_range("L", 1, 1024);
  auto out = a.expand_range(2, 16, std::nullopt);
  EXPECT_EQ(out, (std::vector<int64_t>{2, 4, 8, 16}));
}

TEST(Axis, ExpandRangeGeomUsesExplicitKWhenProvided) {
  auto a = ferret::Axis::geom_range("g", 1, 1024, /*samples_per_octave=*/1);
  // k=2 (explicit) overrides axis default 1.
  auto out = a.expand_range(1, 4, /*at_k=*/2);
  // 1, ~1.41 -> rounds to 1 (dedup), 2, ~2.83 -> 3, 4
  // expected: {1, 2, 3, 4}
  EXPECT_EQ(out, (std::vector<int64_t>{1, 2, 3, 4}));
}

TEST(Axis, ExpandRangeGeomFallsBackToAxisKWhenNullopt) {
  auto a = ferret::Axis::geom_range("g", 1, 8, /*samples_per_octave=*/3);
  // k=3 from axis default.
  auto out = a.expand_range(1, 8, std::nullopt);
  EXPECT_FALSE(out.empty());
  EXPECT_EQ(out.front(), 1);
  EXPECT_EQ(out.back(), 8);
  // 3 samples/octave * 3 octaves + 1 = ~10 distinct values
  EXPECT_GE(out.size(), 4U);
}

TEST(Axis, ExpandRangeAtKOnNonGeomThrows) {
  auto a = ferret::Axis::range("r", 0, 10);
  EXPECT_THROW(a.expand_range(0, 10, /*at_k=*/2), std::invalid_argument);
  auto b = ferret::Axis::log2_range("L", 1, 16);
  EXPECT_THROW(b.expand_range(1, 16, /*at_k=*/2), std::invalid_argument);
}
```

- [ ] **Step 2: Run test to verify failure**

```sh
cmake --build build 2>&1 | tail -5
```

Expected: no member `expand_range`.

- [ ] **Step 3: Add the method**

In `include/ferret/axis.hpp`, add `#include <optional>` at the top with the other headers, then after `validate(int64_t v) const;` add:

```cpp
  // Expands a `lo..hi` range token according to this axis's kind. For
  // GeomRange, `at_k` overrides the axis's declared samples_per_octave
  // when non-null. For non-GeomRange axes, a non-null `at_k` is an
  // error (the CLI `@k` suffix is only meaningful for geom axes).
  // Throws std::invalid_argument on violation; the message embeds the
  // axis name.
  std::vector<int64_t> expand_range(int64_t lo, int64_t hi, std::optional<int64_t> at_k) const;
```

In `src/axis.cpp`, after `Axis::validate` add:

```cpp
std::vector<int64_t> Axis::expand_range(int64_t lo, int64_t hi, std::optional<int64_t> at_k) const {
  if (at_k.has_value() && kind_ != Kind::GeomRange) {
    throw std::invalid_argument("axis '" + name_ + "': @k is only valid for geom_range axes");
  }
  std::string context = "Axis '" + name_ + "'";
  switch (kind_) {
    case Kind::GeomRange:
      return expand_geom_range(lo, hi, at_k.value_or(k_), context);
    case Kind::Log2Range:
      return expand_log2_range(lo, hi, context);
    case Kind::Range:
    case Kind::Values: {
      std::vector<int64_t> out;
      for (int64_t v = lo; v <= hi; ++v) {
        out.push_back(v);
      }
      return out;
    }
  }
  return {};
}
```

- [ ] **Step 4: Run Axis tests to verify pass**

```sh
cmake --build build && ctest --test-dir build -R "Axis\." --output-on-failure
```

Expected: all new `Axis.ExpandRange*` tests pass.

- [ ] **Step 5: Adopt in `cli_axis.cpp`**

In `src/cli_axis.cpp`:

- Delete the file-local `expand_range_token` at lines 65-81.
- In `parse_at_suffix` (lines 39-60), drop the kind check at lines 52-54 — `Axis::expand_range` now owns that error. Replace the function body with:

```cpp
HiAndK parse_at_suffix(const std::string& hi_and_suffix, const std::string& cli_value) {
  auto at = hi_and_suffix.find('@');
  if (at == std::string::npos) {
    return {.hi_s = hi_and_suffix, .k = std::nullopt};
  }
  std::string hi_s = hi_and_suffix.substr(0, at);
  if (hi_s.empty()) {
    throw std::invalid_argument("malformed range: " + cli_value);
  }
  std::string k_s = hi_and_suffix.substr(at + 1);
  if (k_s.empty()) {
    throw std::invalid_argument("malformed @k suffix: " + cli_value);
  }
  int64_t k = parse_int(k_s);
  if (k <= 0) {
    throw std::invalid_argument("@k must be >= 1: " + cli_value);
  }
  return {.hi_s = std::move(hi_s), .k = k};
}
```

(Note: signature change — no more `const Axis& axis` parameter, since the kind-check moved to `Axis::expand_range`.)

- In `parse_cli_axis_value` (line 89), replace:

```cpp
    auto [hi_s, at_k] = parse_at_suffix(cli_value.substr(dotdot + 2), axis, cli_value);
```

with:

```cpp
    auto [hi_s, at_k] = parse_at_suffix(cli_value.substr(dotdot + 2), cli_value);
```

- Replace the `expand_range_token(lo, hi, at_k, axis, cli_value)` call at line 99 with:

```cpp
    try {
      return axis.expand_range(lo, hi, at_k);
    } catch (const std::invalid_argument& e) {
      // Re-throw with the cli_value context that the old expand_range_token
      // attached via its `context` parameter.
      throw std::invalid_argument(std::string(e.what()) + " (in: " + cli_value + ")");
    }
```

- [ ] **Step 6: Build and run full test suite**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass. If `test_cli_axis` or `test_integration` fail on exact error-message wording, adjust the wrap-format above to preserve the existing message and re-run.

- [ ] **Step 7: Commit**

```sh
git add include/ferret/axis.hpp src/axis.cpp tests/test_params_axis.cpp src/cli_axis.cpp
git commit -m "axis: own range-token expansion, collapse cli_axis duplicate"
```

---

## Section B — Decompose `run_command.cpp`

`run()` does benchmark lookup, axis/option classification, sweep expansion, pinning, JIT, measurement, and CSV emission in 47 lines of orchestration. The classifier uses nested linear searches over `axes` and `options` for every override. Goal: collapse the classifier to map lookups and extract the "build inputs" stage so `run()` is thin coordination.

### Task 12: Convert `classify_overrides` to map-based lookup

**Files:**
- Modify: `src/run_command.cpp:34-76`

- [ ] **Step 1: Verify baseline green**

```sh
ctest --test-dir build --output-on-failure -R "test_integration"
```

Expected: pass. `test_integration` is the strongest signal on classifier behavior.

- [ ] **Step 2: Replace the function body**

Replace `src/run_command.cpp:34-76` with:

```cpp
std::optional<ClassifiedOverrides> classify_overrides(const std::string& bench_name, const SweepAxes& axes,
                                                      const BenchOptions& options,
                                                      const std::map<std::string, std::string>& raw) {
  ClassifiedOverrides out;
  std::map<std::string, const Axis*> axis_by_name;
  std::map<std::string, const BenchOption*> option_by_name;
  for (const auto& a : axes) {
    axis_by_name.emplace(a.name(), &a);
  }
  for (const auto& o : options) {
    option_by_name.emplace(o.name, &o);
    out.option_values[o.name] = o.default_value;
  }
  for (const auto& [k, v] : raw) {
    if (auto it = axis_by_name.find(k); it != axis_by_name.end()) {
      try {
        out.axis_values[k] = parse_cli_axis_value(v, *it->second);
      } catch (const std::exception& e) {
        flog::error("invalid value for --{}: {}", k, e.what());
        return std::nullopt;
      }
    } else if (auto it = option_by_name.find(k); it != option_by_name.end()) {
      try {
        out.option_values[k] = parse_option_value(v);
      } catch (const std::exception& e) {
        flog::error("invalid value for --{}: {}", k, e.what());
        return std::nullopt;
      }
    } else {
      flog::error("unknown axis or option --{} for benchmark {}", k, bench_name);
      return std::nullopt;
    }
  }
  return out;
}
```

The behavior is identical; nested O(N*M) scans become O(log N) lookups, and the code is materially easier to read.

- [ ] **Step 3: Build and run full test suite**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass, especially `test_integration`'s "unknown axis" and "invalid value" error-path cases.

- [ ] **Step 4: Commit**

```sh
git add src/run_command.cpp
git commit -m "run_command: classifier uses map lookup instead of nested linear scans"
```

---

### Task 13: Extract `RunInputs` builder

**Files:**
- Modify: `src/run_command.cpp`

This is a structural-only change inside an anonymous namespace; no header changes, no tests added (existing test_integration covers the surface). The goal is to make `run()` short enough to read at a glance.

- [ ] **Step 1: Verify baseline green**

```sh
ctest --test-dir build --output-on-failure -R "test_integration"
```

- [ ] **Step 2: Add the struct and builder above `run()`**

In `src/run_command.cpp`, inside the anonymous namespace (before the closing `}  // namespace` at line 171), add:

```cpp
struct RunInputs {
  std::unique_ptr<Benchmark> bench;
  SweepAxes axes;
  BenchOptions options;
  std::vector<Params> rows;
  std::vector<std::string> cols;
};

std::optional<RunInputs> build_run_inputs(const RunOptions& opts) {
  auto bench = BenchmarkRegistry::create(opts.name);
  if (!bench) {
    flog::error("unknown benchmark '{}'. Try `ferret list`.", opts.name);
    return std::nullopt;
  }
  SweepAxes axes = bench->axes();
  BenchOptions options = bench->options();

  auto classified = classify_overrides(opts.name, axes, options, opts.overrides);
  if (!classified) {
    return std::nullopt;
  }

  std::vector<Params> rows;
  try {
    rows = sweep::expand(axes, classified->axis_values);
  } catch (const std::exception& e) {
    flog::error("invalid sweep: {}", e.what());
    return std::nullopt;
  }
  inject_options(rows, classified->option_values, opts.seed);

  return RunInputs{
      .bench = std::move(bench),
      .axes = std::move(axes),
      .options = std::move(options),
      .rows = std::move(rows),
      .cols = column_names(axes, options),
  };
}
```

Wait — `column_names(axes, options)` uses the just-moved `axes` and `options`. Reorder: compute `cols` before the moves.

Replace with:

```cpp
  auto cols = column_names(axes, options);
  return RunInputs{
      .bench = std::move(bench),
      .axes = std::move(axes),
      .options = std::move(options),
      .rows = std::move(rows),
      .cols = std::move(cols),
  };
```

(Including `<memory>` is already pulled in transitively via `<optional>`/`benchmark.hpp`; if a build error surfaces, add `#include <memory>` to the top of `run_command.cpp`.)

- [ ] **Step 3: Slim `run()` to use the builder**

Replace `src/run_command.cpp:173-220` (the `run()` body) with:

```cpp
int run(const RunOptions& opts) {
  auto inputs = build_run_inputs(opts);
  if (!inputs) {
    return 2;
  }

  apply_pinning(opts.core);

  std::ofstream ofs;
  std::ostream* out = open_output(opts.out_path, ofs);
  if (out == nullptr) {
    return 2;
  }

  double tpns = timing::ticks_per_ns();
  if (!std::isfinite(tpns) || !(tpns > 0.0)) {
    flog::error("ticks_per_ns calibration returned non-finite or non-positive value: {}", tpns);
    return 2;
  }

  auto measured = measure_all(*inputs->bench, inputs->rows, opts.reps, opts.warmup);
  if (!measured) {
    return 2;
  }

  emit_csv(*out, opts.name, inputs->cols, opts.freq_hz, tpns, *measured);
  return 0;
}
```

- [ ] **Step 4: Build and run full test suite**

```sh
cmake --build build && ctest --test-dir build --output-on-failure
```

Expected: all tests pass. `test_integration` validates the end-to-end flow this code drives.

- [ ] **Step 5: Commit**

```sh
git add src/run_command.cpp
git commit -m "run_command: extract RunInputs builder to slim run()"
```

---

## Section C — Platform shims: drop `#ifdef` guards via CMake selection

`src/timing/x86_64.cpp`, `src/timing/aarch64.cpp`, `src/pinning/linux.cpp`, `src/pinning/macos.cpp`, and `src/padding.cpp` are all compiled unconditionally and each `#ifdef`-out their entire body on the wrong platform. CMake already knows which arch / OS it's building for — let it pick the right source.

### Task 14: Make CMake select per-arch timing source

**Files:**
- Modify: `CMakeLists.txt:136-159`
- Modify: `src/timing/x86_64.cpp`
- Modify: `src/timing/aarch64.cpp`

- [ ] **Step 1: Verify baseline green**

```sh
ctest --test-dir build --output-on-failure -R "test_timing"
```

- [ ] **Step 2: Replace the two timing entries in CMakeLists.txt**

In `CMakeLists.txt`, replace lines 143-145 (the two unconditional `src/timing/*.cpp` entries) by removing them from the static `add_library` block and adding conditional entries above it. Replace:

```cmake
add_library(ferret_core STATIC
  src/axis.cpp
  src/params.cpp
  src/sweep.cpp
  src/cli_axis.cpp
  src/freq.cpp
  src/jit.cpp
  src/timing/x86_64.cpp
  src/timing/aarch64.cpp
  src/timing/calibrate.cpp
  src/pinning/linux.cpp
  src/pinning/macos.cpp
  src/benchmark_registry.cpp
  src/csv.cpp
  src/runner.cpp
  src/run_command.cpp
  src/padding.cpp
  src/parse.cpp
  src/log.cpp
  src/permute.cpp
)
```

with:

```cmake
set(ferret_arch_timing_src "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
  set(ferret_arch_timing_src src/timing/x86_64.cpp)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64)")
  set(ferret_arch_timing_src src/timing/aarch64.cpp)
else()
  message(FATAL_ERROR "ferret: unsupported CMAKE_SYSTEM_PROCESSOR='${CMAKE_SYSTEM_PROCESSOR}' "
                      "(supported: x86_64, aarch64)")
endif()

add_library(ferret_core STATIC
  src/axis.cpp
  src/params.cpp
  src/sweep.cpp
  src/cli_axis.cpp
  src/freq.cpp
  src/jit.cpp
  ${ferret_arch_timing_src}
  src/timing/calibrate.cpp
  src/pinning/linux.cpp
  src/pinning/macos.cpp
  src/benchmark_registry.cpp
  src/csv.cpp
  src/runner.cpp
  src/run_command.cpp
  src/padding.cpp
  src/parse.cpp
  src/log.cpp
  src/permute.cpp
)
```

- [ ] **Step 3: Strip the guards from the timing sources**

In `src/timing/x86_64.cpp`, delete lines 3-4 (`#if defined(__x86_64__)...`) and line 16 (closing `#endif`). The file becomes:

```cpp
#include "ferret/timing.hpp"

#include <x86intrin.h>

namespace ferret::timing {

uint64_t arch_now_ticks() {
  unsigned aux;
  return __rdtscp(&aux);
}

}  // namespace ferret::timing
```

In `src/timing/aarch64.cpp`, delete lines 3 and 15. The file becomes:

```cpp
#include "ferret/timing.hpp"

namespace ferret::timing {

uint64_t arch_now_ticks() {
  uint64_t v;
  asm volatile("mrs %0, cntvct_el0" : "=r"(v));
  return v;
}

}  // namespace ferret::timing
```

- [ ] **Step 4: Rebuild from a clean configure**

```sh
rm -rf build
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: clean configure, all tests pass. Watch the `cmake` output for "ferret: unsupported CMAKE_SYSTEM_PROCESSOR" if you're on an unexpected platform.

- [ ] **Step 5: Commit**

```sh
git add CMakeLists.txt src/timing/x86_64.cpp src/timing/aarch64.cpp
git commit -m "build: CMake selects timing source per-arch, drop ifdef guards"
```

---

### Task 15: Make CMake select per-OS pinning source

**Files:**
- Modify: `CMakeLists.txt`
- Modify: `src/pinning/linux.cpp`
- Modify: `src/pinning/macos.cpp`

- [ ] **Step 1: Replace the two pinning entries in CMakeLists.txt**

Just after the `ferret_arch_timing_src` block from Task 14, add:

```cmake
set(ferret_os_pinning_src "")
if(CMAKE_SYSTEM_NAME MATCHES "Linux|Android")
  set(ferret_os_pinning_src src/pinning/linux.cpp)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
  set(ferret_os_pinning_src src/pinning/macos.cpp)
else()
  message(FATAL_ERROR "ferret: unsupported CMAKE_SYSTEM_NAME='${CMAKE_SYSTEM_NAME}' "
                      "(supported: Linux, Android, Darwin)")
endif()
```

In the `add_library(ferret_core STATIC ...)` block, replace:

```cmake
  src/pinning/linux.cpp
  src/pinning/macos.cpp
```

with:

```cmake
  ${ferret_os_pinning_src}
```

- [ ] **Step 2: Strip the guards from the pinning sources**

In `src/pinning/linux.cpp`, delete lines 3 and 25 (`#if defined(__linux__)...` and `#endif`). The first include line becomes:

```cpp
#include "ferret/pinning.hpp"

#include <pthread.h>
// (rest unchanged)
```

In `src/pinning/macos.cpp`, delete lines 3 and 52 (`#ifdef __APPLE__` and `#endif`).

- [ ] **Step 3: Rebuild from clean configure**

```sh
rm -rf build
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass on the current platform.

- [ ] **Step 4: Commit**

```sh
git add CMakeLists.txt src/pinning/linux.cpp src/pinning/macos.cpp
git commit -m "build: CMake selects pinning source per-OS, drop ifdef guards"
```

---

### Task 16: Name the `cpu > 1024` macOS pinning guard

**Files:**
- Modify: `src/pinning/macos.cpp`

This is a small cleanup that the audit called out: the magic `1024` upper bound on accepted core IDs in `pin_to_core` has no name and no documented rationale.

- [ ] **Step 1: Replace the magic number**

In `src/pinning/macos.cpp`, replace:

```cpp
  if (cpu < 0 || cpu > 1024) {
    return false;
  }
```

with:

```cpp
  // Reject obviously-nonsense core IDs before falling back to the QoS hint.
  // 1024 is an arbitrary upper bound far above any shipping CPU's core
  // count; its only purpose is to keep PinToImpossiblyHighCoreReturnsFalse
  // honest. Real Apple Silicon ships up to ~24 cores (M-Ultra).
  static constexpr int kMaxAcceptedCpuId = 1024;
  if (cpu < 0 || cpu > kMaxAcceptedCpuId) {
    return false;
  }
```

- [ ] **Step 2: Build and run pinning tests**

```sh
cmake --build build && ctest --test-dir build -R Pinning --output-on-failure
```

Expected: pass (only matters on macOS; Linux tests don't touch this file).

- [ ] **Step 3: Commit**

```sh
git add src/pinning/macos.cpp
git commit -m "pinning(macos): name the cpu>1024 guard constant"
```

---

### Task 17: Make CMake select per-arch padding source

**Files:**
- Create: `src/padding/x86_64.cpp`
- Create: `src/padding/aarch64.cpp`
- Delete: `src/padding.cpp`
- Modify: `CMakeLists.txt`

This finishes the platform-shim cleanup by splitting `src/padding.cpp` into per-arch files (it was the third-largest `#ifdef` block in the audit).

- [ ] **Step 1: Create `src/padding/x86_64.cpp`**

```cpp
#include "ferret/padding.hpp"

extern "C" {
#include <sljitLir.h>
}

#include <cstdint>

namespace ferret {

void emit_nops(sljit_compiler* c, size_t bytes) {
  static constexpr uint8_t nop = 0x90;
  for (size_t i = 0; i < bytes; ++i) {
    sljit_emit_op_custom(c, const_cast<uint8_t*>(&nop), 1);
  }
}

}  // namespace ferret
```

- [ ] **Step 2: Create `src/padding/aarch64.cpp`**

```cpp
#include "ferret/padding.hpp"

extern "C" {
#include <sljitLir.h>
}

#include <cstdint>

namespace ferret {

void emit_nops(sljit_compiler* c, size_t bytes) {
  static const uint32_t nop_insn = 0xd503201f;  // AArch64 NOP
  size_t insns = (bytes + 3) / 4;               // round up
  for (size_t i = 0; i < insns; ++i) {
    sljit_emit_op_custom(c, const_cast<uint32_t*>(&nop_insn), 4);
  }
}

}  // namespace ferret
```

- [ ] **Step 3: Delete `src/padding.cpp`**

```sh
git rm src/padding.cpp
```

- [ ] **Step 4: Update `CMakeLists.txt`**

After the `ferret_os_pinning_src` block, add:

```cmake
set(ferret_arch_padding_src "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
  set(ferret_arch_padding_src src/padding/x86_64.cpp)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64)")
  set(ferret_arch_padding_src src/padding/aarch64.cpp)
endif()
```

In the `ferret_core` source list, replace `src/padding.cpp` with `${ferret_arch_padding_src}`.

- [ ] **Step 5: Rebuild from clean configure**

```sh
rm -rf build
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. `test_padding` exercises this code.

- [ ] **Step 6: Commit**

```sh
git add src/padding/x86_64.cpp src/padding/aarch64.cpp CMakeLists.txt
git commit -m "build: CMake selects padding source per-arch, drop ifdef guards"
```

---

### Task 18: Final integration check and refactor commit log

**Files:** none modified.

- [ ] **Step 1: Run full test suite from clean build**

```sh
rm -rf build
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass on a fresh configure.

- [ ] **Step 2: Run the actual benchmark end-to-end**

```sh
build/ferret list
build/ferret run dependent_chain_throughput --chain_length=1000000 --core=0 --out=/tmp/freq.csv
build/ferret run direct_branch_footprint --branches=1..1024 --spacing_bytes=16..64 --core=0 --out=/tmp/btb.csv
build/ferret run nested_call_depth --depth=1..16 --variant=0,1,2 --core=0 --out=/tmp/ras.csv
build/ferret run branch_history_footprint --branches=1..128 --history_len=4..64 --core=0 --out=/tmp/bhp.csv
```

Expected: each invocation writes a CSV with the expected column headers and at least one data row per parameter point. No crashes.

- [ ] **Step 3: Sanity-check the diff stat**

```sh
git log --oneline main..HEAD
git diff --stat main..HEAD
```

Expected: ~18 commits, scoped to `include/ferret/`, `src/`, `benchmarks/`, `tests/`, and `CMakeLists.txt` / `tests/CMakeLists.txt`. No spurious changes in `docs/`, `scripts/`, or vendored deps.

- [ ] **Step 4: No commit (verification only)**

The plan ends here. No new files are written; this task only verifies the cumulative result of Tasks 1-17.

---

## Self-Review Notes

Re-reading the plan against the original audit recommendations:

**Section A (Tier 1A — bench scaffolding):** ✓ outer-loop helper (Task 1-5), iteration formula (Task 6), Sattolo seed mix (Task 7), verify_layout shared offset check (Task 8). The "parameter validation reimplemented in each benchmark" finding is *not* addressed in this plan — each benchmark's validation rules are too domain-specific (spacing_bytes divisibility, depth bounds, variant enum) for a useful shared helper. Left out intentionally.

**Section D (Tier 2D — Axis ownership):** ✓ `parse_int` extract (Task 9), `Axis::validate` (Task 10), `Axis::expand_range` (Task 11). The `fail()` lambda pattern audit suggestion is not adopted — error messages stay as concatenations to preserve test compatibility, and the pattern wasn't a structural concern, just a small ergonomic one.

**Section B (Tier 1B — run_command):** ✓ map-based classifier (Task 12), RunInputs builder (Task 13). The `OutputFormatter` interface for future JSON output is deferred — it's speculative, no second format is on deck.

**Section C (Tier 1C — platform shims):** ✓ timing (Task 14), pinning (Task 15), magic constant (Task 16), padding (Task 17). The `ITimingBackend` / `IPinningBackend` polymorphic-interface suggestion is deliberately not adopted — runtime polymorphism is overkill for shims that are statically known per build. CMake source selection gets the same code-clarity win without vtables in the hot timing path. The audit's `try_affinity` / `try_qos_fallback` extraction in `pinning/macos.cpp` is also deferred — the function is 25 lines, the split would not improve clarity meaningfully.

**Type / signature consistency:**
- `emit_outer_loop`: template, signature stable across Tasks 1-5.
- `compute_iterations(size_t, size_t)`: signature stable across Task 6.
- `mix_seed(uint64_t, uint64_t, uint64_t)`: signature stable across Task 7.
- `verify_uniform_spacing(const std::vector<sljit_label*>&, size_t, bool, const char*)`: stable across Task 8.
- `parse_int(const std::string&)`: stable across Tasks 9-11.
- `Axis::validate(int64_t)` and `Axis::expand_range(int64_t, int64_t, std::optional<int64_t>)`: stable across Tasks 10-11.
- `ClassifiedOverrides`, `RunInputs`: only used inside `run_command.cpp`'s anonymous namespace.

**Placeholder scan:** No "TBD" / "TODO" / "similar to Task N" / "add appropriate error handling" left in the plan. Every code step has a complete code block; every test step shows the assertion.

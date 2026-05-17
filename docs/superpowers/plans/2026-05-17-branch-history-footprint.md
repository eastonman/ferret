# branch_history_footprint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add ferret's fourth benchmark, `branch_history_footprint`, which sweeps `(branches, history_len)` and emits data-dependent conditional branches that target the next instruction — isolating BPU direction-predictor capacity from BTB target prediction.

**Architecture:** Single benchmark file under `benchmarks/`. Per outer iteration, compute `row_ptr = flat_base + hist_idx * branches * 4`; each of `branches` sites does one `ldr/mov` with a baked per-branch displacement followed by `cbnz/jecxz` targeting the next instruction. Memory is a flat `uint32_t[history_len][branches]` (transposed for sequential L1 access) filled with random 0/1 once per parameter point. Buffer pointer is baked into the JIT as `SLJIT_IMM`; runner unchanged.

**Tech Stack:** C++20, sljit (JIT IR), GoogleTest, CMake. Uses `sljit_emit_op_custom` for the per-site hand-emitted byte sequences (matches `direct_branch_footprint` precedent).

**Spec:** [`docs/superpowers/specs/2026-05-17-branch-history-footprint-design.md`](../specs/2026-05-17-branch-history-footprint-design.md)

**Worktree:** Already created at `/Users/easton/WorkingSpace/project/ferret/feat/branch-history-footprint` on branch `feat/branch-history-footprint`. All work happens there.

---

## File Map

| Path | Action | Responsibility |
| ---- | ------ | -------------- |
| `benchmarks/branch_history_footprint.cpp` | Create | Benchmark subclass + registration + internal encoders/fill in `branch_history_footprint_internal` namespace |
| `tests/test_branch_history_footprint.cpp` | Create | Unit tests for axes/options/validation/fill-determinism/encoders/layout |
| `tests/test_integration.cpp` | Modify | Append three `Integration.BranchHistoryFootprint*` tests |
| `tests/CMakeLists.txt` | Modify | Add `test_branch_history_footprint` target |
| `CMakeLists.txt` | Modify | Add `benchmarks/branch_history_footprint.cpp` to the `ferret` executable source list |
| `docs/benchmarks/branch_history_footprint.md` | Create | Per-benchmark doc page (kernel structure, options, reading curves) |
| `README.md` | Modify | Add row to the benchmarks table |

---

## Pre-flight (do once before Task 1)

- [ ] **Verify you are in the worktree and on the right branch**

```bash
cd /Users/easton/WorkingSpace/project/ferret/feat/branch-history-footprint
git status
git branch --show-current
```

Expected:
```
On branch feat/branch-history-footprint
nothing to commit, working tree clean
feat/branch-history-footprint
```

- [ ] **Verify the spec exists and the build is green at baseline**

```bash
test -f docs/superpowers/specs/2026-05-17-branch-history-footprint-design.md && echo "spec OK"
nix develop --command cmake -S . -B build -GNinja
nix develop --command cmake --build build
nix develop --command ctest --test-dir build --output-on-failure
```

Expected: `spec OK`, clean build, all existing tests pass. If tests fail at baseline, **stop and surface to user** — do not proceed.

---

### Task 1: Scaffold the benchmark class

**Files:**
- Create: `benchmarks/branch_history_footprint.cpp`
- Modify: `tests/CMakeLists.txt` (append new test target)
- Create: `tests/test_branch_history_footprint.cpp`

- [ ] **Step 1: Write the failing registry test**

Create `tests/test_branch_history_footprint.cpp`:

```cpp
#include <gtest/gtest.h>

#include "ferret/benchmark.hpp"

TEST(BranchHistoryFootprint, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "branch_history_footprint");
}
```

- [ ] **Step 2: Add the test target to CMake**

Edit `tests/CMakeLists.txt`. Find the block for `test_nested_call_depth` (around line 94) and add **after** it:

```cmake
add_executable(test_branch_history_footprint
  test_branch_history_footprint.cpp
  ../benchmarks/branch_history_footprint.cpp
)
target_link_libraries(test_branch_history_footprint PRIVATE
  ferret_core
  sljit::sljit
  GTest::gtest GTest::gtest_main
)
gtest_discover_tests(test_branch_history_footprint)
```

- [ ] **Step 3: Create the empty benchmark file**

Create `benchmarks/branch_history_footprint.cpp`:

```cpp
extern "C" {
#include <sljitLir.h>
}

#include <cstddef>
#include <string>

#include "ferret/benchmark.hpp"

namespace ferret {

struct BranchHistoryFootprint : Benchmark {
  std::string name() const override { return "branch_history_footprint"; }
  SweepAxes axes() const override { return {}; }
  size_t sites_per_kernel(const Params& /*p*/) const override { return 1; }
  size_t iterations(const Params& /*p*/) const override { return 1; }
  void emit_kernel(sljit_compiler* /*c*/, const Params& /*p*/) override {}
};

FERRET_BENCHMARK("branch_history_footprint", BranchHistoryFootprint);

}  // namespace ferret
```

- [ ] **Step 4: Build and run the test**

```bash
nix develop --command cmake --build build --target test_branch_history_footprint
nix develop --command ctest --test-dir build -R BranchHistoryFootprint --output-on-failure
```

Expected: PASS. The benchmark is registered and the registry returns it.

- [ ] **Step 5: Commit**

```bash
git add benchmarks/branch_history_footprint.cpp tests/test_branch_history_footprint.cpp tests/CMakeLists.txt
git commit --no-gpg-sign -m "feat(branch_history_footprint): scaffold benchmark class + registry test"
```

---

### Task 2: Axes, sites_per_kernel, iterations

**Files:**
- Modify: `benchmarks/branch_history_footprint.cpp`
- Modify: `tests/test_branch_history_footprint.cpp`

- [ ] **Step 1: Write the failing axes/sites/iterations tests**

Append to `tests/test_branch_history_footprint.cpp`:

```cpp
#include <algorithm>
#include <vector>

namespace {
ferret::Params make_params(int64_t branches, int64_t history_len,
                           int64_t pattern = 1, int64_t spacing = 16) {
  ferret::Params p;
  p.set("branches", branches);
  p.set("history_len", history_len);
  p.set("pattern", pattern);
  p.set("spacing_bytes", spacing);
  p.set("seed", 1);
  return p;
}
}  // namespace

TEST(BranchHistoryFootprint, ExposesBranchesAndHistoryLenAxes) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  ASSERT_EQ(axes.size(), 2u);
  EXPECT_EQ(axes[0].name(), "branches");
  EXPECT_EQ(axes[1].name(), "history_len");
}

TEST(BranchHistoryFootprint, BranchesAxisExpansionMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[0].expand();
  // 1..512 with k=1: {1,2,4,8,16,32,64,128,256,512} = 10 points.
  EXPECT_EQ(vs.size(), 10u);
  EXPECT_EQ(vs.front(), 1);
  EXPECT_EQ(vs.back(), 512);
}

TEST(BranchHistoryFootprint, HistoryLenAxisExpansionMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[1].expand();
  // 4..4096 with k=1: {4,8,16,32,64,128,256,512,1024,2048,4096} = 11 points.
  EXPECT_EQ(vs.size(), 11u);
  EXPECT_EQ(vs.front(), 4);
  EXPECT_EQ(vs.back(), 4096);
}

TEST(BranchHistoryFootprint, SitesPerKernelEqualsBranches) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->sites_per_kernel(make_params(32, 64)), 32u);
  EXPECT_EQ(b->sites_per_kernel(make_params(1, 4)), 1u);
}

TEST(BranchHistoryFootprint, IterationsAmortizesAtTenMillionSites) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->iterations(make_params(1, 4)), 10'000'000u);
  EXPECT_EQ(b->iterations(make_params(100, 4)), 100'000u);
  EXPECT_EQ(b->iterations(make_params(10'000'001, 4)), 1u);  // clamped at >=1
}
```

- [ ] **Step 2: Implement axes/sites/iterations**

Replace the placeholder methods in `benchmarks/branch_history_footprint.cpp`:

```cpp
#include <algorithm>  // std::max

// ... inside the struct ...

[[nodiscard]] SweepAxes axes() const override {
  return {
      Axis::geom_range("branches", 1, 1 << 9, /*k=*/1),    // 1..512
      Axis::geom_range("history_len", 4, 1 << 12, /*k=*/1),  // 4..4096
  };
}

[[nodiscard]] size_t sites_per_kernel(const Params& p) const override {
  return p.get<size_t>("branches");
}

[[nodiscard]] size_t iterations(const Params& p) const override {
  return std::max<size_t>(1, 10'000'000 / p.get<size_t>("branches"));
}
```

- [ ] **Step 3: Build and run**

```bash
nix develop --command cmake --build build --target test_branch_history_footprint
nix develop --command ctest --test-dir build -R BranchHistoryFootprint --output-on-failure
```

Expected: all 5 BranchHistoryFootprint tests PASS.

- [ ] **Step 4: Commit**

```bash
git add benchmarks/branch_history_footprint.cpp tests/test_branch_history_footprint.cpp
git commit --no-gpg-sign -m "feat(branch_history_footprint): axes, sites_per_kernel, iterations"
```

---

### Task 3: Per-bench options (`pattern`, `spacing_bytes`)

**Files:**
- Modify: `benchmarks/branch_history_footprint.cpp`
- Modify: `tests/test_branch_history_footprint.cpp`

- [ ] **Step 1: Write failing options tests**

Append to `tests/test_branch_history_footprint.cpp`:

```cpp
namespace {
const ferret::BenchOption* find_option(const ferret::BenchOptions& opts, const std::string& name) {
  for (const auto& o : opts) {
    if (o.name == name) return &o;
  }
  return nullptr;
}
}  // namespace

TEST(BranchHistoryFootprint, ExposesPatternAndSpacingBytesOptions) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 2u);

  const auto* pattern = find_option(opts, "pattern");
  ASSERT_NE(pattern, nullptr);
  EXPECT_EQ(pattern->default_value, 1);  // random

  const auto* spacing = find_option(opts, "spacing_bytes");
  ASSERT_NE(spacing, nullptr);
  EXPECT_EQ(spacing->default_value, 16);
}
```

- [ ] **Step 2: Implement `options()`**

Add to the struct in `benchmarks/branch_history_footprint.cpp`:

```cpp
[[nodiscard]] BenchOptions options() const override {
  return {
      BenchOption{.name = "pattern", .default_value = 1},        // 1 = random, 0 = zero
      BenchOption{.name = "spacing_bytes", .default_value = 16},
  };
}
```

- [ ] **Step 3: Build and run**

```bash
nix develop --command cmake --build build --target test_branch_history_footprint
nix develop --command ctest --test-dir build -R BranchHistoryFootprint --output-on-failure
```

Expected: PASS.

- [ ] **Step 4: Commit**

```bash
git add benchmarks/branch_history_footprint.cpp tests/test_branch_history_footprint.cpp
git commit --no-gpg-sign -m "feat(branch_history_footprint): pattern + spacing_bytes options"
```

---

### Task 4: Pre-codegen validation in `emit_kernel`

Reject invalid parameters *before* any sljit calls so the compiler state stays clean. This is the same discipline `direct_branch_footprint` uses.

**Files:**
- Modify: `benchmarks/branch_history_footprint.cpp`
- Modify: `tests/test_branch_history_footprint.cpp`

- [ ] **Step 1: Write failing validation tests**

Append to `tests/test_branch_history_footprint.cpp`:

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

TEST(BranchHistoryFootprint, RejectsSpacingBytesTooSmall) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  // min site is 8 bytes on AArch64, 9 on x86_64. 4 is below both.
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 4, /*pattern=*/1, /*spacing=*/4)),
               std::invalid_argument);
}

TEST(BranchHistoryFootprint, RejectsSpacingBytesMisaligned) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
#if defined(__aarch64__) || defined(_M_ARM64)
  // AArch64 needs 4-byte alignment. 9 is not divisible by 4.
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 4, 1, /*spacing=*/9)),
               std::invalid_argument);
#else
  // x86_64 has kBranchAlign=1; any positive value >= kMinSiteBytes is valid.
  // Use a value that is below kMinSiteBytes to keep the test meaningful.
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 4, 1, /*spacing=*/5)),
               std::invalid_argument);
#endif
}

TEST(BranchHistoryFootprint, RejectsZeroBranches) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(0, 4)), std::invalid_argument);
}

TEST(BranchHistoryFootprint, RejectsZeroHistoryLen) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 0)), std::invalid_argument);
}

TEST(BranchHistoryFootprint, RejectsInvalidPattern) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 4, /*pattern=*/2)),
               std::invalid_argument);
}

#if defined(__aarch64__) || defined(_M_ARM64)
TEST(BranchHistoryFootprint, RejectsBranchesAboveAArch64LdrImmediateLimit) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(/*branches=*/4096, 4)),
               std::invalid_argument);
}
#endif
```

- [ ] **Step 2: Implement validation**

Add to `benchmarks/branch_history_footprint.cpp` *inside* the `ferret` namespace, **above** the `BranchHistoryFootprint` struct:

```cpp
namespace {

// Per-arch site-encoding constants. Used both for pre-codegen validation
// in emit_kernel and for the hand-emitted op_custom byte sequences.
#if defined(__aarch64__) || defined(_M_ARM64)
constexpr size_t kBranchAlign = 4;
constexpr size_t kMinSiteBytes = 8;   // 4 (ldr) + 4 (cbnz)
constexpr size_t kMaxBranches = 4095;  // AArch64 ldr unsigned-imm-12 scaled by 4
#elif defined(__x86_64__) || defined(_M_X64)
constexpr size_t kBranchAlign = 1;
constexpr size_t kMinSiteBytes = 9;   // 7 (mov r/m32 with REX+disp32) + 2 (jecxz)
constexpr size_t kMaxBranches = static_cast<size_t>(INT32_MAX) / 4;
#else
#error "ferret v1 supports only x86_64 and aarch64"
#endif

}  // namespace
```

Then replace the `emit_kernel` body to do validation only:

```cpp
void emit_kernel(sljit_compiler* /*c*/, const Params& p) override {
  auto branches = p.get<size_t>("branches");
  auto history_len = p.get<size_t>("history_len");
  auto pattern = p.get<int64_t>("pattern");
  auto spacing = p.get<size_t>("spacing_bytes");

  if (branches < 1) {
    throw std::invalid_argument("branches must be >= 1");
  }
  if (history_len < 1) {
    throw std::invalid_argument("history_len must be >= 1");
  }
  if (pattern != 0 && pattern != 1) {
    throw std::invalid_argument("pattern must be 0 (zero) or 1 (random); got " +
                                std::to_string(pattern));
  }
  if (spacing < kMinSiteBytes) {
    throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                " is smaller than the site encoding (" +
                                std::to_string(kMinSiteBytes) + " bytes) on this architecture");
  }
  if (spacing % kBranchAlign != 0) {
    throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                " must be a multiple of " +
                                std::to_string(kBranchAlign) + " on this architecture");
  }
  if (branches > kMaxBranches) {
    throw std::invalid_argument("branches=" + std::to_string(branches) +
                                " exceeds the per-arch limit of " +
                                std::to_string(kMaxBranches));
  }
}
```

Add includes at the top of the file:

```cpp
#include <cstdint>
#include <stdexcept>
```

- [ ] **Step 3: Build and run**

```bash
nix develop --command cmake --build build --target test_branch_history_footprint
nix develop --command ctest --test-dir build -R BranchHistoryFootprint --output-on-failure
```

Expected: all validation tests PASS.

- [ ] **Step 4: Commit**

```bash
git add benchmarks/branch_history_footprint.cpp tests/test_branch_history_footprint.cpp
git commit --no-gpg-sign -m "feat(branch_history_footprint): pre-codegen parameter validation"
```

---

### Task 5: Pattern buffer fill (deterministic random / zero baseline)

The benchmark owns a `std::vector<uint32_t> flat_` sized to `branches * history_len`. The fill function is exposed via an `_internal` namespace so the test can pin determinism without touching the JIT.

**Files:**
- Modify: `benchmarks/branch_history_footprint.cpp`
- Modify: `tests/test_branch_history_footprint.cpp`

- [ ] **Step 1: Write failing fill tests**

Append to `tests/test_branch_history_footprint.cpp`:

```cpp
#include <cstdint>

namespace ferret::branch_history_footprint_internal {
// Exposed for unit testing; defined in benchmarks/branch_history_footprint.cpp.
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t history_len,
                                            int64_t pattern, uint64_t seed);
}  // namespace ferret::branch_history_footprint_internal

TEST(BranchHistoryFootprint, ZeroPatternProducesAllZeros) {
  auto v = ferret::branch_history_footprint_internal::generate_pattern_fill(
      /*branches=*/4, /*history_len=*/8, /*pattern=*/0, /*seed=*/1);
  ASSERT_EQ(v.size(), 32u);
  for (uint32_t x : v) EXPECT_EQ(x, 0u);
}

TEST(BranchHistoryFootprint, RandomPatternIsDeterministicForSameSeed) {
  auto a = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 1, 42);
  auto b = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 1, 42);
  EXPECT_EQ(a, b);
}

TEST(BranchHistoryFootprint, RandomPatternDiffersBetweenSeeds) {
  auto a = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 1, 42);
  auto b = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 1, 43);
  EXPECT_NE(a, b);
}

TEST(BranchHistoryFootprint, RandomPatternDiffersByParamPoint) {
  // Seed mix includes (branches, history_len), so different points diverge.
  auto a = ferret::branch_history_footprint_internal::generate_pattern_fill(4, 16, 1, 1);
  auto b = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 1, 1);
  // Different sizes → not directly comparable; check that the prefix differs.
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
  auto v = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 32, 1, 7);
  for (uint32_t x : v) {
    EXPECT_TRUE(x == 0u || x == 1u) << "value out of {0,1}: " << x;
  }
}
```

- [ ] **Step 2: Implement `generate_pattern_fill`**

Add to `benchmarks/branch_history_footprint.cpp`. Place **outside** the `BranchHistoryFootprint` struct but inside the `ferret` namespace:

```cpp
#include <random>

namespace branch_history_footprint_internal {

std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t history_len,
                                            int64_t pattern, uint64_t seed) {
  std::vector<uint32_t> flat(branches * history_len, 0u);
  if (pattern == 0) {
    return flat;
  }
  // Mix seed with (branches, history_len) so distinct grid points get
  // distinct fills. Same constants as direct_branch_footprint uses for
  // its Sattolo seed mix — keeps the seed convention consistent.
  uint64_t mixed = seed
                 ^ (static_cast<uint64_t>(branches) * 0x9E3779B97F4A7C15ULL)
                 ^ (static_cast<uint64_t>(history_len) * 0xBF58476D1CE4E5B9ULL);
  std::mt19937_64 rng(mixed);
  for (auto& v : flat) v = static_cast<uint32_t>(rng() & 1u);
  return flat;
}

}  // namespace branch_history_footprint_internal
```

- [ ] **Step 3: Build and run**

```bash
nix develop --command cmake --build build --target test_branch_history_footprint
nix develop --command ctest --test-dir build -R BranchHistoryFootprint --output-on-failure
```

Expected: 5 new fill tests PASS.

- [ ] **Step 4: Commit**

```bash
git add benchmarks/branch_history_footprint.cpp tests/test_branch_history_footprint.cpp
git commit --no-gpg-sign -m "feat(branch_history_footprint): seeded deterministic pattern fill"
```

---

### Task 6: Per-arch site byte encoders (hand-emitted opcodes)

Each site is emitted via `sljit_emit_op_custom` with a precise byte sequence. The encoders are pure functions of `(target_reg_idx, base_reg_idx, displacement)` — easy to unit-test without invoking sljit. Test these in isolation before wiring into the JIT.

**Files:**
- Modify: `benchmarks/branch_history_footprint.cpp`
- Modify: `tests/test_branch_history_footprint.cpp`

- [ ] **Step 1: Write failing encoder tests**

Append to `tests/test_branch_history_footprint.cpp`:

```cpp
#include <array>

namespace ferret::branch_history_footprint_internal {
#if defined(__aarch64__) || defined(_M_ARM64)
// `ldr Wt, [Xn, #imm12*4]` (32-bit, unsigned offset). Returns 4 LE bytes.
std::array<uint8_t, 4> encode_aarch64_ldr_w(unsigned rt, unsigned rn, unsigned imm12);
// `cbnz Wt, .+4` (branch to next instruction). Returns 4 LE bytes.
std::array<uint8_t, 4> encode_aarch64_cbnz_w_next(unsigned rt);
#elif defined(__x86_64__) || defined(_M_X64)
// `mov r32, [base + disp32]` with disp32 forced. Returns 6 or 7 bytes
// (7 with REX.B when base is r8..r15). `target_reg_idx` is 1 (ecx)
// in our usage; `base_reg_idx` is whatever sljit_get_register_index
// returned for the saved register we picked.
std::vector<uint8_t> encode_x86_64_mov_r32_base_disp32(unsigned target_reg_idx,
                                                       unsigned base_reg_idx,
                                                       int32_t disp);
// `jecxz rel8=0` — 2 bytes, targets next instruction.
std::array<uint8_t, 2> encode_x86_64_jecxz_next();
#endif
}  // namespace ferret::branch_history_footprint_internal

#if defined(__aarch64__) || defined(_M_ARM64)
TEST(BranchHistoryFootprint, AArch64LdrW_ZeroOffset) {
  // ldr w13, [x14, #0] = 0xB94001CD → LE bytes CD 01 40 B9.
  auto bytes = ferret::branch_history_footprint_internal::encode_aarch64_ldr_w(13, 14, 0);
  EXPECT_EQ(bytes[0], 0xCD);
  EXPECT_EQ(bytes[1], 0x01);
  EXPECT_EQ(bytes[2], 0x40);
  EXPECT_EQ(bytes[3], 0xB9);
}

TEST(BranchHistoryFootprint, AArch64LdrW_ImmediateScaling) {
  // ldr w13, [x14, #4] → imm12=1 → 0xB94005CD → LE CD 05 40 B9.
  auto bytes = ferret::branch_history_footprint_internal::encode_aarch64_ldr_w(13, 14, 1);
  EXPECT_EQ(bytes[0], 0xCD);
  EXPECT_EQ(bytes[1], 0x05);
  EXPECT_EQ(bytes[2], 0x40);
  EXPECT_EQ(bytes[3], 0xB9);
}

TEST(BranchHistoryFootprint, AArch64CbnzWNextInstruction) {
  // cbnz w13, .+4 → imm19=1, Rt=13 → 0x3500002D → LE 2D 00 00 35.
  auto bytes = ferret::branch_history_footprint_internal::encode_aarch64_cbnz_w_next(13);
  EXPECT_EQ(bytes[0], 0x2D);
  EXPECT_EQ(bytes[1], 0x00);
  EXPECT_EQ(bytes[2], 0x00);
  EXPECT_EQ(bytes[3], 0x35);
}
#endif

#if defined(__x86_64__) || defined(_M_X64)
TEST(BranchHistoryFootprint, X86_64Mov_Ecx_FromRbx_Disp32) {
  // mov ecx, [rbx + 0x12345678]:
  //   opcode 8B, ModRM = 10 001 011 = 0x8B, disp32 little-endian.
  //   No REX needed (rbx is encoded in low 8 regs).
  auto bytes = ferret::branch_history_footprint_internal::encode_x86_64_mov_r32_base_disp32(
      /*target_reg_idx=*/1 /*ecx*/, /*base_reg_idx=*/3 /*rbx*/, 0x12345678);
  ASSERT_EQ(bytes.size(), 6u);
  EXPECT_EQ(bytes[0], 0x8B);
  EXPECT_EQ(bytes[1], 0x8B);
  EXPECT_EQ(bytes[2], 0x78);
  EXPECT_EQ(bytes[3], 0x56);
  EXPECT_EQ(bytes[4], 0x34);
  EXPECT_EQ(bytes[5], 0x12);
}

TEST(BranchHistoryFootprint, X86_64Mov_Ecx_FromR14_Disp32) {
  // mov ecx, [r14 + 0x00000010]:
  //   REX = 0x41 (B=1 to extend r/m), opcode 8B, ModRM = 10 001 110 = 0x8E.
  auto bytes = ferret::branch_history_footprint_internal::encode_x86_64_mov_r32_base_disp32(
      /*target_reg_idx=*/1, /*base_reg_idx=*/14, 0x00000010);
  ASSERT_EQ(bytes.size(), 7u);
  EXPECT_EQ(bytes[0], 0x41);
  EXPECT_EQ(bytes[1], 0x8B);
  EXPECT_EQ(bytes[2], 0x8E);
  EXPECT_EQ(bytes[3], 0x10);
  EXPECT_EQ(bytes[4], 0x00);
  EXPECT_EQ(bytes[5], 0x00);
  EXPECT_EQ(bytes[6], 0x00);
}

TEST(BranchHistoryFootprint, X86_64JecxzNext) {
  auto bytes = ferret::branch_history_footprint_internal::encode_x86_64_jecxz_next();
  EXPECT_EQ(bytes[0], 0xE3);
  EXPECT_EQ(bytes[1], 0x00);
}
#endif
```

- [ ] **Step 2: Implement the encoders**

Add to `benchmarks/branch_history_footprint.cpp` inside the `branch_history_footprint_internal` namespace:

```cpp
#if defined(__aarch64__) || defined(_M_ARM64)

std::array<uint8_t, 4> encode_aarch64_ldr_w(unsigned rt, unsigned rn, unsigned imm12) {
  // LDR (immediate), 32-bit, unsigned offset:
  //   1 0 1 1 1 0 0 1 0 1 imm12 Rn Rt
  //   = 0xB9400000 | (imm12 << 10) | (rn << 5) | rt
  uint32_t insn = 0xB9400000u | ((imm12 & 0xFFFu) << 10) | ((rn & 0x1Fu) << 5) | (rt & 0x1Fu);
  return {static_cast<uint8_t>(insn & 0xFF),
          static_cast<uint8_t>((insn >> 8) & 0xFF),
          static_cast<uint8_t>((insn >> 16) & 0xFF),
          static_cast<uint8_t>((insn >> 24) & 0xFF)};
}

std::array<uint8_t, 4> encode_aarch64_cbnz_w_next(unsigned rt) {
  // CBNZ (32-bit), offset = +4 (next insn) → imm19 = 1:
  //   0 0 1 1 0 1 0 1 imm19 Rt
  //   = 0x35000000 | (imm19 << 5) | rt
  constexpr unsigned imm19 = 1;
  uint32_t insn = 0x35000000u | ((imm19 & 0x7FFFFu) << 5) | (rt & 0x1Fu);
  return {static_cast<uint8_t>(insn & 0xFF),
          static_cast<uint8_t>((insn >> 8) & 0xFF),
          static_cast<uint8_t>((insn >> 16) & 0xFF),
          static_cast<uint8_t>((insn >> 24) & 0xFF)};
}

#elif defined(__x86_64__) || defined(_M_X64)

std::vector<uint8_t> encode_x86_64_mov_r32_base_disp32(unsigned target_reg_idx,
                                                       unsigned base_reg_idx,
                                                       int32_t disp) {
  // mov r32, [base + disp32]:
  //   Optional REX (0x41 with B=1) when base is r8..r15 (idx >= 8).
  //   Or REX (0x44) with R=1 when target is r8d..r15d. Our usage pins
  //   target to ECX (idx 1), so only REX.B matters in practice.
  //   Opcode: 0x8B
  //   ModRM:  mod=10 (disp32), reg=target_reg_idx & 7, r/m=base_reg_idx & 7
  std::vector<uint8_t> out;
  uint8_t rex = 0x40;
  bool need_rex = false;
  if (target_reg_idx >= 8) {
    rex |= 0x04;  // REX.R
    need_rex = true;
  }
  if (base_reg_idx >= 8) {
    rex |= 0x01;  // REX.B
    need_rex = true;
  }
  if (need_rex) out.push_back(rex);
  out.push_back(0x8B);
  uint8_t modrm = static_cast<uint8_t>(
      (2 << 6) | ((target_reg_idx & 0x7) << 3) | (base_reg_idx & 0x7));
  // r/m = 100 (=4) means SIB follows, and r/m = 101 (=5) with mod=00
  // means [disp32]-only — both encodings we must avoid here. Our
  // intended bases (sljit's saved registers) never map to these slots
  // when forced through disp32; verify at runtime if a future sljit
  // version reassigns.
  out.push_back(modrm);
  out.push_back(static_cast<uint8_t>(disp & 0xFF));
  out.push_back(static_cast<uint8_t>((disp >> 8) & 0xFF));
  out.push_back(static_cast<uint8_t>((disp >> 16) & 0xFF));
  out.push_back(static_cast<uint8_t>((disp >> 24) & 0xFF));
  return out;
}

std::array<uint8_t, 2> encode_x86_64_jecxz_next() {
  return {0xE3, 0x00};
}

#endif
```

Also add at the top of the file:

```cpp
#include <array>
#include <vector>
```

**Note on the SIB/disp32 edge case:** the implementation comment above flags
that base register indices 4 (`rsp`/`r12` low 3 bits = 100) and 5 (`rbp`/`r13`
low 3 bits = 101) collide with SIB / disp32-only encoding. The kernel
emitter (Task 7) must verify the actual sljit-mapped base register isn't
one of those and throw with a clear message if it is — we don't expect
sljit to assign those for SLJIT_S* saveds, but the runtime check is cheap
and prevents silent miscompilation if sljit changes.

- [ ] **Step 3: Build and run**

```bash
nix develop --command cmake --build build --target test_branch_history_footprint
nix develop --command ctest --test-dir build -R BranchHistoryFootprint --output-on-failure
```

Expected: all encoder tests PASS (only the per-arch ones for your host).

- [ ] **Step 4: Commit**

```bash
git add benchmarks/branch_history_footprint.cpp tests/test_branch_history_footprint.cpp
git commit --no-gpg-sign -m "feat(branch_history_footprint): per-arch site byte encoders"
```

---

### Task 7: Kernel emission + `verify_layout`

This task wires the encoders into a complete sljit-emitted kernel. The shape (from spec §4.3):

1. Prologue (sljit primitives): load `flat_base`, `branches*4`, `history_len`, `iters` into registers; init `hist_idx = 0`.
2. Outer-loop top: compute `row_ptr = flat_base + hist_idx * (branches*4)`.
3. Chain: for each branch `j` in `0..branches-1`, emit the site as `op_custom(load[row_ptr, j*4])` + `op_custom(cbnz/jecxz next)` + NOP padding to `spacing_bytes`. Record the label that marks the start of each site for `verify_layout`.
4. Outer-loop tail: `hist_idx = (hist_idx+1 == history_len) ? 0 : hist_idx+1`; decrement iters; back-edge.
5. `verify_layout`: assert each site's offset from labels[0] equals `i * spacing_bytes`.

**Files:**
- Modify: `benchmarks/branch_history_footprint.cpp`
- Modify: `tests/test_branch_history_footprint.cpp`

- [ ] **Step 1: Write the failing emit + verify_layout test**

Append to `tests/test_branch_history_footprint.cpp`:

```cpp
namespace ferret::branch_history_footprint_internal {
// Exposed for unit testing the post-codegen layout.
struct LayoutSnapshot {
  std::vector<sljit_label*> labels;
  size_t branches;
  size_t spacing;
};
LayoutSnapshot last_layout_snapshot();
}  // namespace ferret::branch_history_footprint_internal

TEST(BranchHistoryFootprint, EmitsValidKernelForSmallParams) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*branches=*/4, /*history_len=*/8,
                       /*pattern=*/1, /*spacing=*/16);
  ASSERT_NO_THROW(b->emit_kernel(ch.c, p));
  ASSERT_EQ(sljit_get_compiler_error(ch.c), SLJIT_SUCCESS);

  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);

  ASSERT_NO_THROW(b->verify_layout(ch.c));

  // Spacing-equal-to-min site must also work:
#if defined(__aarch64__) || defined(_M_ARM64)
  CompilerHandle ch2;
  ASSERT_NO_THROW(b->emit_kernel(
      ch2.c, make_params(2, 4, /*pattern=*/0, /*spacing=*/8)));
  ASSERT_NE(sljit_generate_code(ch2.c, 0, nullptr), nullptr);
  ASSERT_NO_THROW(b->verify_layout(ch2.c));
#endif

  sljit_free_code(code, nullptr);
}

TEST(BranchHistoryFootprint, LayoutSnapshotMatchesSpacing) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  b->emit_kernel(ch.c, make_params(/*branches=*/4, /*history_len=*/4,
                                    /*pattern=*/0, /*spacing=*/16));
  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  b->verify_layout(ch.c);  // Must not throw.

  auto snap = ferret::branch_history_footprint_internal::last_layout_snapshot();
  ASSERT_EQ(snap.branches, 4u);
  ASSERT_EQ(snap.spacing, 16u);
  ASSERT_EQ(snap.labels.size(), 5u);  // branches + 1 (chain exit)
  sljit_uw base = sljit_get_label_addr(snap.labels[0]);
  for (size_t i = 1; i <= snap.branches; ++i) {
    sljit_uw addr = sljit_get_label_addr(snap.labels[i]);
    EXPECT_EQ(addr - base, i * snap.spacing)
        << "site " << i << " not at i*spacing";
  }
  sljit_free_code(code, nullptr);
}
```

- [ ] **Step 2: Implement member state + emit_kernel + verify_layout**

Replace the `BranchHistoryFootprint` struct in `benchmarks/branch_history_footprint.cpp` with the full implementation. Add includes first:

```cpp
#include "ferret/padding.hpp"
```

Then the struct (replace the existing one wholesale):

```cpp
struct BranchHistoryFootprint : Benchmark {
  // Per-emission state, lives across emit_kernel → verify_layout for
  // one parameter point. Reused on subsequent points after resize().
  std::vector<uint32_t> flat_;
  std::vector<sljit_label*> last_labels_;
  size_t last_branches_ = 0;
  size_t last_spacing_ = 0;

  [[nodiscard]] std::string name() const override { return "branch_history_footprint"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::geom_range("branches", 1, 1 << 9, 1),
        Axis::geom_range("history_len", 4, 1 << 12, 1),
    };
  }

  [[nodiscard]] BenchOptions options() const override {
    return {
        BenchOption{.name = "pattern", .default_value = 1},
        BenchOption{.name = "spacing_bytes", .default_value = 16},
    };
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override {
    return p.get<size_t>("branches");
  }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return std::max<size_t>(1, 10'000'000 / p.get<size_t>("branches"));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override {
    auto branches = p.get<size_t>("branches");
    auto history_len = p.get<size_t>("history_len");
    auto pattern = p.get<int64_t>("pattern");
    auto spacing = p.get<size_t>("spacing_bytes");
    auto seed = static_cast<uint64_t>(p.get<int64_t>("seed"));
    size_t iters = iterations(p);

    // --- Validation (must come before any sljit calls). ---
    if (branches < 1) throw std::invalid_argument("branches must be >= 1");
    if (history_len < 1) throw std::invalid_argument("history_len must be >= 1");
    if (pattern != 0 && pattern != 1) {
      throw std::invalid_argument("pattern must be 0 (zero) or 1 (random); got " +
                                  std::to_string(pattern));
    }
    if (spacing < kMinSiteBytes) {
      throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                  " is smaller than the site encoding (" +
                                  std::to_string(kMinSiteBytes) + " bytes) on this architecture");
    }
    if (spacing % kBranchAlign != 0) {
      throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                  " must be a multiple of " +
                                  std::to_string(kBranchAlign) + " on this architecture");
    }
    if (branches > kMaxBranches) {
      throw std::invalid_argument("branches=" + std::to_string(branches) +
                                  " exceeds the per-arch limit of " +
                                  std::to_string(kMaxBranches));
    }

    // --- Allocate + fill the pattern buffer. ---
    flat_ = branch_history_footprint_internal::generate_pattern_fill(
        branches, history_len, pattern, seed);

    // --- sljit prologue: 4 scratches + 2 saveds (row_ptr base, hist_idx). ---
    // SLJIT_S0 holds `flat_base` for the lifetime of the call.
    // SLJIT_S1 holds the running history index (hist_idx).
    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/4, /*saved=*/2, /*local_size=*/0);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_S0, 0, SLJIT_IMM,
                   reinterpret_cast<sljit_sw>(flat_.data()));
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 0);  // hist_idx = 0
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM,
                   static_cast<sljit_sw>(iters));

    // --- Per-arch base/row register indices for op_custom site emit. ---
    // SLJIT_R1 holds row_ptr (recomputed per outer iter).
    // SLJIT_R2 holds the load destination (the value we test).
    const unsigned base_reg_idx = static_cast<unsigned>(
        sljit_get_register_index(SLJIT_GP_REGISTER, SLJIT_R1));
    const unsigned target_reg_idx = static_cast<unsigned>(
        sljit_get_register_index(SLJIT_GP_REGISTER, SLJIT_R2));

#if defined(__x86_64__) || defined(_M_X64)
    // The hand-emitted mov uses ModRM r/m = (base_reg_idx & 7). r/m=4
    // means SIB-byte follows (would require a different encoder); r/m=5
    // with mod=10 is legal but we still want to be sure base_reg_idx is
    // not 4 (rsp) or 12 (r12), which would also need a SIB byte for
    // the encoding we picked. Defensive runtime check:
    if ((base_reg_idx & 0x7u) == 4u) {
      throw std::runtime_error(
          "branch_history_footprint: sljit assigned base register idx " +
          std::to_string(base_reg_idx) +
          " which conflicts with SIB-required ModRM encoding; "
          "kernel emitter needs an updated encoder");
    }
    // jecxz tests ECX; pin target to ECX (idx 1). If sljit assigned
    // anything else to SLJIT_R2 we must re-route; throw rather than
    // miscompile.
    if (target_reg_idx != 1u) {
      throw std::runtime_error(
          "branch_history_footprint: expected SLJIT_R2 → ECX (idx 1), "
          "got idx " + std::to_string(target_reg_idx));
    }
#endif

    // --- Outer-loop top. ---
    sljit_label* loop_top = sljit_emit_label(c);

    // row_ptr = flat_base + hist_idx * (branches * 4)
    // sljit's MUL is 64-bit; product is sljit_sw-wide.
    sljit_emit_op2(c, SLJIT_MUL, SLJIT_R1, 0,
                   SLJIT_S1, 0, SLJIT_IMM, static_cast<sljit_sw>(branches * 4));
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_R1, 0,
                   SLJIT_R1, 0, SLJIT_S0, 0);

    // --- Branch chain: branches sites at PC = chain_start + i * spacing. ---
    last_labels_.clear();
    last_labels_.reserve(branches + 1);

    for (size_t j = 0; j < branches; ++j) {
      sljit_label* site = sljit_emit_label(c);
      last_labels_.push_back(site);

      // Emit the per-arch site: load + cond branch to next instr.
#if defined(__aarch64__) || defined(_M_ARM64)
      auto ldr = branch_history_footprint_internal::encode_aarch64_ldr_w(
          target_reg_idx, base_reg_idx, static_cast<unsigned>(j));
      sljit_emit_op_custom(c, ldr.data(), 4);
      auto cbnz = branch_history_footprint_internal::encode_aarch64_cbnz_w_next(
          target_reg_idx);
      sljit_emit_op_custom(c, cbnz.data(), 4);
#elif defined(__x86_64__) || defined(_M_X64)
      auto mov = branch_history_footprint_internal::encode_x86_64_mov_r32_base_disp32(
          target_reg_idx, base_reg_idx, static_cast<int32_t>(j * 4));
      sljit_emit_op_custom(c, mov.data(), static_cast<sljit_u32>(mov.size()));
      auto jcxz = branch_history_footprint_internal::encode_x86_64_jecxz_next();
      sljit_emit_op_custom(c, jcxz.data(), 2);
#endif

      // Padding to spacing_bytes.
      emit_nops(c, spacing - kMinSiteBytes);
    }
    // Chain-exit label (one past the last site).
    last_labels_.push_back(sljit_emit_label(c));

    // --- hist_idx wrap. ---
    // hist_idx = (hist_idx + 1 == history_len) ? 0 : hist_idx + 1
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0,
                   SLJIT_S1, 0, SLJIT_IMM, 1);
    sljit_emit_op2u(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_S1, 0,
                    SLJIT_IMM, static_cast<sljit_sw>(history_len));
    sljit_jump* not_eq = sljit_emit_jump(c, SLJIT_NOT_ZERO);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 0);
    sljit_label* after_wrap = sljit_emit_label(c);
    sljit_set_label(not_eq, after_wrap);

    // --- Outer-loop tail: dec iters, back-edge. ---
    sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R0, 0,
                   SLJIT_R0, 0, SLJIT_IMM, 1);
    sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
    sljit_set_label(back, loop_top);

    sljit_emit_return_void(c);

    last_branches_ = branches;
    last_spacing_ = spacing;
  }

  void verify_layout(sljit_compiler* /*c*/) override {
    if (last_branches_ == 0 || last_labels_.empty()) return;
    sljit_uw base = sljit_get_label_addr(last_labels_[0]);
    for (size_t i = 1; i <= last_branches_; ++i) {
      sljit_uw addr = sljit_get_label_addr(last_labels_[i]);
      auto actual = static_cast<size_t>(addr - base);
      size_t expected = i * last_spacing_;
      if (actual != expected) {
        auto delta = static_cast<int64_t>(actual) - static_cast<int64_t>(expected);
        throw std::runtime_error("branch_history_footprint: site " + std::to_string(i) +
                                 " at offset " + std::to_string(actual) +
                                 ", expected " + std::to_string(expected) +
                                 " (delta " + std::to_string(delta) + ")");
      }
    }
  }
};
```

Then expose the layout snapshot for the test. Add to the `branch_history_footprint_internal` namespace:

```cpp
// Test-only accessor for the most recent emit_kernel layout.
// Returns a copy; safe to call after sljit_generate_code.
namespace {
const BranchHistoryFootprint* g_last_emitted = nullptr;
}

struct LayoutSnapshot {
  std::vector<sljit_label*> labels;
  size_t branches;
  size_t spacing;
};

LayoutSnapshot last_layout_snapshot() {
  if (!g_last_emitted) return {};
  return {g_last_emitted->last_labels_, g_last_emitted->last_branches_,
          g_last_emitted->last_spacing_};
}
```

And at the end of `emit_kernel`, set `g_last_emitted = this;` (just before the function returns). This is for testing only — outside of tests, the pointer just tracks whichever instance last emitted.

**Forward-declare `BranchHistoryFootprint` above the `branch_history_footprint_internal` namespace** so `g_last_emitted` and the snapshot signature are well-formed:

```cpp
namespace ferret {
struct BranchHistoryFootprint;  // forward decl
namespace branch_history_footprint_internal {
// ... fill + encoders + LayoutSnapshot + last_layout_snapshot() ...
}
struct BranchHistoryFootprint : Benchmark { ... };
}  // ferret
```

- [ ] **Step 3: Build and run**

```bash
nix develop --command cmake --build build --target test_branch_history_footprint
nix develop --command ctest --test-dir build -R BranchHistoryFootprint --output-on-failure
```

Expected: all tests PASS, including the new emit + layout tests.

If sljit_generate_code returns `nullptr` on AArch64, double-check `sljit_emit_enter` ordering and that `sljit_get_compiler_error(c)` is `SLJIT_SUCCESS` immediately before generate.

- [ ] **Step 4: Commit**

```bash
git add benchmarks/branch_history_footprint.cpp tests/test_branch_history_footprint.cpp
git commit --no-gpg-sign -m "feat(branch_history_footprint): emit_kernel + verify_layout"
```

---

### Task 8: Wire the benchmark into the main `ferret` executable

So far the benchmark is only compiled into the test executable. Add it to the main binary so `ferret list` and `ferret run branch_history_footprint` work.

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Edit the root CMakeLists**

Find the `add_executable(ferret ...)` block (around line 163-167) and add `benchmarks/branch_history_footprint.cpp` to the source list:

```cmake
add_executable(ferret
  src/main.cpp
  benchmarks/dependent_chain_throughput.cpp
  benchmarks/direct_branch_footprint.cpp
  benchmarks/nested_call_depth.cpp
  benchmarks/branch_history_footprint.cpp
)
```

- [ ] **Step 2: Rebuild and verify it appears in `ferret list`**

```bash
nix develop --command cmake --build build --target ferret
build/ferret list
```

Expected: output includes `branch_history_footprint` alongside the other three benchmark names.

- [ ] **Step 3: Smoke-run the binary on a tiny sweep**

```bash
build/ferret run branch_history_footprint --branches=1,2 --history_len=4 --reps=2 --warmup=1 --out=/tmp/bhf_smoke.csv
cat /tmp/bhf_smoke.csv
```

Expected: a header row plus 2 data rows; all cells populated.

- [ ] **Step 4: Commit**

```bash
git add CMakeLists.txt
git commit --no-gpg-sign -m "build(branch_history_footprint): add to ferret executable source list"
```

---

### Task 9: Integration tests (CSV smoke runs)

Append three tests to the integration suite that match the patterns the existing benchmarks use.

**Files:**
- Modify: `tests/test_integration.cpp`

- [ ] **Step 1: Write the failing integration tests**

Append to `tests/test_integration.cpp` (place after the existing `Integration.DirectBranchFootprint*` tests, before the error-handling tests):

```cpp
TEST(Integration, BranchHistoryFootprintProducesExpectedRowCount) {
  auto out = std::filesystem::temp_directory_path() / "ferret_bhf.csv";
  std::filesystem::remove(out);
  // branches ∈ {1,2,4} × history_len ∈ {4,8} → 6 data rows.
  std::string cmd = std::string(FERRET_BINARY) +
                    " run branch_history_footprint"
                    " --branches=1..4 --history_len=4..8"
                    " --pattern=0"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 7u);  // header + 6 data rows
  EXPECT_EQ(contents.find(",,\n"), std::string::npos);
}

TEST(Integration, BranchHistoryFootprintHeaderHasExpectedColumns) {
  auto out = std::filesystem::temp_directory_path() / "ferret_bhf_hdr.csv";
  std::filesystem::remove(out);
  std::string cmd = std::string(FERRET_BINARY) +
                    " run branch_history_footprint"
                    " --branches=1 --history_len=4"
                    " --pattern=0 --reps=2 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  EXPECT_NE(contents.find("branches"), std::string::npos);
  EXPECT_NE(contents.find("history_len"), std::string::npos);
  EXPECT_NE(contents.find("pattern"), std::string::npos);
  EXPECT_NE(contents.find("spacing_bytes"), std::string::npos);
}

TEST(Integration, BranchHistoryFootprintRandomPatternProducesNonEmptyRows) {
  auto out = std::filesystem::temp_directory_path() / "ferret_bhf_rand.csv";
  std::filesystem::remove(out);
  std::string cmd = std::string(FERRET_BINARY) +
                    " run branch_history_footprint"
                    " --branches=1,2 --history_len=4,8"
                    " --pattern=1 --seed=7"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 5u);  // header + 4 rows
  EXPECT_EQ(contents.find(",,"), std::string::npos);
}
```

- [ ] **Step 2: Build and run**

```bash
nix develop --command cmake --build build
nix develop --command ctest --test-dir build -R "Integration.BranchHistoryFootprint" --output-on-failure
```

Expected: 3 new integration tests PASS.

- [ ] **Step 3: Commit**

```bash
git add tests/test_integration.cpp
git commit --no-gpg-sign -m "test(branch_history_footprint): CSV smoke integration tests"
```

---

### Task 10: Sanitizer pass

Run the existing test suite under ASan+UBSan to catch any pointer-arithmetic mistakes in the JIT-emitted indexing or in the buffer allocation path.

- [ ] **Step 1: Configure and build with sanitizers**

```bash
nix develop --command cmake -S . -B build-asan -GNinja -DFERRET_SANITIZER=address+undefined
nix develop --command cmake --build build-asan
```

- [ ] **Step 2: Run the new tests under sanitizers**

```bash
nix develop --command ctest --test-dir build-asan -R "BranchHistoryFootprint|Integration.BranchHistoryFootprint" --output-on-failure
```

Expected: all tests PASS; no ASan/UBSan reports.

- [ ] **Step 3: If failures, fix and re-run (no commit needed if no source changes)**

If a sanitizer flags a real bug, fix it and commit. If no bug, proceed.

---

### Task 11: Per-benchmark doc page

Add a doc page mirroring `docs/benchmarks/direct_branch_footprint.md`'s structure.

**Files:**
- Create: `docs/benchmarks/branch_history_footprint.md`

- [ ] **Step 1: Create the doc page**

```markdown
# `branch_history_footprint` — conditional-branch direction-predictor footprint

`N` data-dependent conditional branches inside an outer loop, with
each branch's taken/not-taken outcome read from a per-branch row of a
flat `uint32_t` buffer indexed by a history position that cycles
through `history_len` once per outer iteration. The branch target is
the immediately-following instruction, so taken and not-taken paths
converge architecturally — BTB target prediction is trivial; only the
direction predictor is exercised.

Per-site cost is flat while the predictor can track all `branches ×
history_len` patterns; once it can't, mispredict rate climbs and
per-site cost steps up.

## Kernel structure

```
   PC                  site (spacing_bytes apart)
 0x0000   ┌──────────────────────────────────────┐
          │  LDR  w13, [row_ptr, #0]             │
          │  CBNZ w13, .Lnext_0                  │ ──┐  branch to next instr
          │ .Lnext_0:                            │ ◄─┘
          │  <NOP pad to spacing_bytes>          │
 base+1×spacing   ┌──────────────────────────────┐
          │  LDR  w13, [row_ptr, #4]             │
          │  CBNZ w13, .Lnext_1                  │ ──┐
          │ .Lnext_1:                            │ ◄─┘
          │  <NOP pad to spacing_bytes>          │
 base+2×spacing   ┌──────────────────────────────┐
          │   ...                                │
          ├──────────────────────────────────────┤
          │  ADD  hist_idx, hist_idx, 1          │
          │  CMP  hist_idx, history_len  → wrap  │
          │  SUBS iters, iters, 1; B.NE loop_top │
          └──────────────────────────────────────┘
```

Annotated:

- The pattern buffer is laid out as `flat[history_len][branches]`
  (transposed). Consecutive branch sites in one outer iteration
  access adjacent words in memory; one L1 line covers 16 sites.
- Each site is `kMinSiteBytes` of code (8 on AArch64, 9 on x86_64)
  plus NOP padding to `spacing_bytes`. Per-branch displacement is
  baked as `j*4` into the load encoding — no per-site address ALU.
- `row_ptr = flat_base + hist_idx * branches * 4` is recomputed once
  per outer iteration; the chain itself never mutates the address
  base.

## Per-benchmark options

| flag                  | meaning                                                  |
| --------------------- | -------------------------------------------------------- |
| `--pattern=0`         | All-zero fill — all-not-taken trivial baseline.          |
| `--pattern=1` (def.)  | Per-entry random `{0,1}` seeded by `--seed`.             |
| `--spacing_bytes=16`  | PC stride per site. Min 8 (AArch64) / 9 (x86_64).        |

## CLI surface

| flag                       | meaning                                                                |
| -------------------------- | ---------------------------------------------------------------------- |
| `--branches=A..B`          | Geometric sweep, default `k=1`, e.g. `1..512`.                         |
| `--branches=A..B@k`        | Geometric sweep with `k` samples per octave.                           |
| `--branches=v1,v2,…`       | Explicit list.                                                         |
| `--history_len=A..B[@k]`   | Same syntax as `--branches`. Default `4..4096`.                        |
| `--pattern=0\|1`           | See above. Default `1`.                                                |
| `--spacing_bytes=N`        | Per-site PC stride. Default 16.                                        |
| `--seed=…`                 | Seeds the per-branch random fill.                                      |

See [`../cli.md`](../cli.md) for global flags.

## Reading the curves

Plot a heatmap with `branches` on one axis and `history_len` on the
other:

```sh
python3 scripts/plot.py heatmap /tmp/bhf.csv --out=/tmp/bhf.png
```

Low region (low `branches × history_len`): per-site cost is flat near
the front-end's branch-per-cycle limit — the predictor handles
everything.

High region: cost steps up; the cliff position is the predictor's
capacity for this workload shape.

Running with `--pattern=0` gives a flat control surface across both
axes (always-not-taken is trivial to predict regardless of count).
Compare against the `--pattern=1` heatmap to confirm the cliff is
predictor-driven, not kernel-driven.

## Caveats

- **Small `history_len` is a near-flat baseline.** The leftmost
  columns of the heatmap (history_len 4, 8) are intended as a
  sanity-check baseline. Modern predictors handle them trivially.
- **`branches=1` carries outer-loop tax.** The chain-tail
  `ADD/CMP/CSEL/SUBS/B.NE` is ~4 cycles per outer iteration regardless
  of `N`. For `branches=1` that's the dominant cost — read the
  leftmost column as a tax baseline, not "one-branch predictor cost."
- **Branch-to-next isolates direction prediction only.** Real-world
  mispredict cost also includes target-redirect latency; this
  benchmark deliberately doesn't measure that.
- **Apple Silicon pinning.** See the project README's discipline
  section — probe and benchmark land on *some* P-core, not
  necessarily the same one.

## Related docs

- Construction rationale: [design spec](../superpowers/specs/2026-05-17-branch-history-footprint-design.md).
- Project two-step workflow: [project README](../../README.md).
```

- [ ] **Step 2: Commit**

```bash
git add docs/benchmarks/branch_history_footprint.md
git commit --no-gpg-sign -m "docs(branch_history_footprint): add per-benchmark page"
```

---

### Task 12: README update

Add `branch_history_footprint` to the benchmarks table in the project README.

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Edit the benchmarks table**

Find the `## Benchmarks` table (around line 75-82) and add a new row **after** `nested_call_depth`:

```markdown
| [`branch_history_footprint`](docs/benchmarks/branch_history_footprint.md)        | conditional-branch direction-predictor capacity |
```

- [ ] **Step 2: Verify rendering**

```bash
grep -A 6 "^## Benchmarks" README.md
```

Expected: 4 rows in the table, the new one between `nested_call_depth` and the closing of the table.

- [ ] **Step 3: Commit**

```bash
git add README.md
git commit --no-gpg-sign -m "docs(readme): list branch_history_footprint in benchmarks table"
```

---

### Task 13: Final full-suite check

- [ ] **Step 1: Clean rebuild and full ctest**

```bash
nix develop --command cmake --build build
nix develop --command ctest --test-dir build --output-on-failure
```

Expected: all tests pass, including the previously passing 3 benchmarks' tests and the new `BranchHistoryFootprint` + `Integration.BranchHistoryFootprint*` suites.

- [ ] **Step 2: Verify the standard two-step workflow works end-to-end**

```bash
build/ferret run dependent_chain_throughput --reps=3 --warmup=1 --out=/tmp/freq.csv
python3 scripts/freq.py /tmp/freq.csv
# (Note the printed estimated_freq value.)

build/ferret run branch_history_footprint --branches=1..16 --history_len=4..32 --reps=3 --warmup=1 --out=/tmp/bhf.csv
python3 scripts/plot.py heatmap /tmp/bhf.csv --out=/tmp/bhf.png 2>&1 | tail -5
```

Expected: probe runs, sweep runs, heatmap plot is generated (or the plot script prints an actionable error — investigate if so).

- [ ] **Step 3: PR prep**

Per saved feedback workflow:
1. Run code-simplifier on changed files:
   - `benchmarks/branch_history_footprint.cpp`
   - `tests/test_branch_history_footprint.cpp`
   - `tests/test_integration.cpp` (only the new tests)
   - `docs/benchmarks/branch_history_footprint.md`
2. Squash fix commits into the feat commits they fix.
3. Then open the PR.

This step is **manual** — don't automate it inside this plan's execution.

---

## Self-Review Notes (recorded after writing)

- **Spec coverage:** all 12 spec sections map to a task: §1-2 → Tasks 1-2; §3 (workflow) → Task 13; §4 (kernel) → Tasks 6-7; §5 (axes/options) → Tasks 2-3; §6 (class shape) → Task 7; §7 (data plumbing) → Task 5; §8 (CSV) → Task 9; §9 (errors) → Task 4; §10 (testing) → Tasks 1-10; §11-12 (known limits / future work) → doc page in Task 11.
- **Placeholders:** none. Every code step contains the actual code.
- **Type consistency:** `flat_` is `std::vector<uint32_t>` throughout; `last_labels_` is `std::vector<sljit_label*>`; member names match between Tasks 5/7 and the spec.
- **Forward-declaration discipline:** Task 7 explicitly notes the forward-decl ordering needed for `g_last_emitted` + `LayoutSnapshot`.

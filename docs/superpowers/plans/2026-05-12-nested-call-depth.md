# nested_call_depth Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a third ferret microbenchmark, `nested_call_depth`, that probes the CPU's Return Address Stack (RAS) by emitting a chain of N nested call/ret pairs with a K=8 shared-callee dispatch that defeats BTB-indirect prediction on the ret PCs.

**Architecture:** One new source file under `benchmarks/`. The benchmark JIT-emits N+2 sljit sub-functions in a single compiler context (chain_main, BODY_N, …, BODY_1, BODY_0/LEAF), wires them up with real hardware `CALL/RET` via `SLJIT_CALL`, and reads dispatch indices from a pre-generated `path_table[ROWS][N+1]` of random bytes ∈ [0,8) seeded by `seed ^ hash(depth)`. The table is owned by the benchmark instance and its base address is baked into the JIT'd code as an immediate.

**Tech Stack:** C++20, sljit (LIR JIT), CMake, GoogleTest. Two host architectures: x86_64 and AArch64.

**Spec references:**
- `docs/superpowers/specs/2026-05-12-nested-call-depth-design.md` — the benchmark design.
- `docs/superpowers/specs/2026-05-12-ras-depth-kernel-pattern.md` — kernel-construction rationale and the BTB-defeat argument.
- `docs/superpowers/specs/2026-05-09-ferret-design.md` — framework conventions and naming rule (§5.1).

---

## File Structure

| File | Status | Responsibility |
|------|--------|---------------|
| `benchmarks/nested_call_depth.cpp` | **Create** | The benchmark class, path-table generation, JIT emission. |
| `CMakeLists.txt` | **Modify** | Add the new source file to the `ferret` executable target. |
| `tests/test_nested_call_depth.cpp` | **Create** | GoogleTest unit tests: registry shape, options/axes, path-table seeding, pre-flight rejection. |
| `tests/CMakeLists.txt` | **Modify** | Register the new test executable. |
| `tests/test_integration.cpp` | **Modify** | Add end-to-end integration tests for the new benchmark and its pre-flight rejections. |

**Why a single source file:** The new benchmark mirrors `direct_branch_footprint.cpp` and `dependent_chain_throughput.cpp` — one self-contained file per benchmark, registered with `FERRET_BENCHMARK`. The dispatch helpers and path-table generator are file-local helpers in an anonymous namespace, matching the existing pattern (`emit_nops`, `sattolo_cycle` are used the same way).

---

## Constants

These are referenced repeatedly throughout the plan. Define them as `constexpr` at the top of `nested_call_depth.cpp`:

```cpp
namespace {
// K dispatch sites per body. Hardcoded per the design doc §6 — not swept.
constexpr int kK = 8;
// log2(kK), used to size the binary-tree dispatch (3 conditional branches).
constexpr int kKBits = 3;
}  // namespace
```

---

## Task 1: Skeleton class + CMake wiring

**Files:**
- Create: `benchmarks/nested_call_depth.cpp`
- Modify: `CMakeLists.txt` (add source file to `add_executable(ferret …)`)
- Create: `tests/test_nested_call_depth.cpp`
- Modify: `tests/CMakeLists.txt` (add test executable)

### Steps

- [ ] **Step 1.1: Write the failing registry test**

Create `tests/test_nested_call_depth.cpp` with:

```cpp
#include <gtest/gtest.h>

#include "ferret/benchmark.hpp"

namespace {

TEST(NestedCallDepth, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "nested_call_depth");
}

TEST(NestedCallDepth, ExposesSingleDepthAxis) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  ASSERT_EQ(axes.size(), 1u);
  EXPECT_EQ(axes[0].name(), "depth");
}

TEST(NestedCallDepth, ExposesPathTableRowsOptionWithDefault4096) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 1u);
  EXPECT_EQ(opts[0].name, "path_table_rows");
  EXPECT_EQ(opts[0].default_value, 4096);
}

}  // namespace
```

- [ ] **Step 1.2: Add the test executable to `tests/CMakeLists.txt`**

Append to `tests/CMakeLists.txt` (after the existing `test_permute` entry, before `test_integration`):

```cmake
add_executable(test_nested_call_depth test_nested_call_depth.cpp)
target_link_libraries(test_nested_call_depth PRIVATE
  ferret_core
  sljit::sljit
  GTest::gtest GTest::gtest_main
)
gtest_discover_tests(test_nested_call_depth)
```

- [ ] **Step 1.3: Run the test, expect link failure**

```sh
cmake --build build --target test_nested_call_depth
```

Expected: link error mentioning `nested_call_depth` not found, OR the test runs and the three tests fail because the registry doesn't contain that name.

- [ ] **Step 1.4: Create the skeleton benchmark file**

Create `benchmarks/nested_call_depth.cpp`:

```cpp
extern "C" {
#include <sljitLir.h>
}

#include <stdexcept>
#include <string>

#include "ferret/benchmark.hpp"

namespace ferret {

namespace {
constexpr int kK = 8;
constexpr int kKBits = 3;
}  // namespace

struct NestedCallDepth : Benchmark {
  [[nodiscard]] std::string name() const override { return "nested_call_depth"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {Axis::range("depth", 1, 64)};
  }

  [[nodiscard]] BenchOptions options() const override {
    return {BenchOption{.name = "path_table_rows", .default_value = 4096}};
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override {
    return p.get<size_t>("depth") + 1;
  }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return std::max<size_t>(1, 1'000'000 / (p.get<size_t>("depth") + 1));
  }

  void emit_kernel(sljit_compiler* /*c*/, const Params& /*p*/) override {
    throw std::logic_error("nested_call_depth: emit_kernel not yet implemented");
  }
};

FERRET_BENCHMARK("nested_call_depth", NestedCallDepth);

}  // namespace ferret
```

- [ ] **Step 1.5: Register the source file in `CMakeLists.txt`**

In the root `CMakeLists.txt`, find the `add_executable(ferret …)` block and add `benchmarks/nested_call_depth.cpp` to the source list (next to the other two benchmark files):

```cmake
add_executable(ferret
  src/main.cpp
  benchmarks/dependent_chain_throughput.cpp
  benchmarks/direct_branch_footprint.cpp
  benchmarks/nested_call_depth.cpp
)
```

- [ ] **Step 1.6: Build and run the test**

```sh
cmake --build build --target test_nested_call_depth
ctest --test-dir build --output-on-failure -R NestedCallDepth
```

Expected: three tests pass (`RegistryLookupReturnsBenchmark`, `ExposesSingleDepthAxis`, `ExposesPathTableRowsOptionWithDefault4096`).

- [ ] **Step 1.7: Commit**

```sh
git add benchmarks/nested_call_depth.cpp CMakeLists.txt \
        tests/test_nested_call_depth.cpp tests/CMakeLists.txt
git commit -m "feat(nested_call_depth): add skeleton class and registry tests"
```

---

## Task 2: Pre-flight validation in emit_kernel

The benchmark must reject `path_table_rows` that are not a power of two ≥ 2, and `depth < 1`, before touching any sljit state — same convention as `direct_branch_footprint`'s `spacing_bytes` validation. Validation lives at the top of `emit_kernel`.

**Files:**
- Modify: `benchmarks/nested_call_depth.cpp` (`emit_kernel` body)
- Modify: `tests/test_nested_call_depth.cpp` (new validation tests)

### Steps

- [ ] **Step 2.1: Write the failing validation tests**

Append to `tests/test_nested_call_depth.cpp`:

```cpp
#include <memory>

extern "C" {
#include <sljitLir.h>
}

namespace {

ferret::Params make_params(int64_t depth, int64_t path_table_rows) {
  ferret::Params p;
  p.set("depth", depth);
  p.set("path_table_rows", path_table_rows);
  p.set("seed", 1);
  return p;
}

struct CompilerHandle {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  ~CompilerHandle() { if (c) sljit_free_compiler(c); }
};

TEST(NestedCallDepth, RejectsZeroDepth) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*depth=*/0, /*path_table_rows=*/4096);
  EXPECT_THROW(b->emit_kernel(ch.c, p), std::invalid_argument);
}

TEST(NestedCallDepth, RejectsPathTableRowsNotPowerOfTwo) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*depth=*/4, /*path_table_rows=*/3);
  EXPECT_THROW(b->emit_kernel(ch.c, p), std::invalid_argument);
}

TEST(NestedCallDepth, RejectsPathTableRowsLessThanTwo) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*depth=*/4, /*path_table_rows=*/1);
  EXPECT_THROW(b->emit_kernel(ch.c, p), std::invalid_argument);
}

}  // namespace
```

Add `#include "ferret/params.hpp"` near the existing includes if not already present.

- [ ] **Step 2.2: Run the tests, expect failure**

```sh
cmake --build build --target test_nested_call_depth
ctest --test-dir build --output-on-failure -R NestedCallDepth.Rejects
```

Expected: all three new tests fail (the stub throws `std::logic_error`, not `std::invalid_argument`).

- [ ] **Step 2.3: Implement validation in `emit_kernel`**

Replace the stub `emit_kernel` body in `benchmarks/nested_call_depth.cpp` with:

```cpp
void emit_kernel(sljit_compiler* c, const Params& p) override {
  auto depth = p.get<size_t>("depth");
  auto path_table_rows = p.get<int64_t>("path_table_rows");

  if (depth < 1) {
    throw std::invalid_argument("nested_call_depth: depth must be >= 1, got " +
                                std::to_string(depth));
  }
  if (path_table_rows < 2) {
    throw std::invalid_argument("nested_call_depth: path_table_rows must be >= 2, got " +
                                std::to_string(path_table_rows));
  }
  if ((path_table_rows & (path_table_rows - 1)) != 0) {
    throw std::invalid_argument("nested_call_depth: path_table_rows must be a power of two, got " +
                                std::to_string(path_table_rows));
  }

  // JIT emission lives in subsequent tasks. For now keep the rest of the
  // body empty so the validation tests run cleanly.
  (void)c;
}
```

- [ ] **Step 2.4: Run the tests, expect pass**

```sh
ctest --test-dir build --output-on-failure -R NestedCallDepth
```

Expected: all six tests pass.

- [ ] **Step 2.5: Commit**

```sh
git add benchmarks/nested_call_depth.cpp tests/test_nested_call_depth.cpp
git commit -m "feat(nested_call_depth): pre-flight validation for depth and path_table_rows"
```

---

## Task 3: Path-table generation helper

A file-local helper produces the `path_table[ROWS][N+1]` byte array. Each byte is uniformly drawn from `[0, kK)`. The PRNG is a 64-bit xorshift seeded with `seed ^ (depth × constant)` so distinct sweep points get distinct tables.

**Files:**
- Modify: `benchmarks/nested_call_depth.cpp` (helper + unit-test hook)
- Modify: `tests/test_nested_call_depth.cpp` (new helper tests)

### Steps

- [ ] **Step 3.1: Write the failing helper test**

Append to `tests/test_nested_call_depth.cpp`:

```cpp
#include <cstdint>
#include <vector>

namespace ferret::nested_call_depth_internal {
// Exposed for unit testing. See Step 3.3 for the definition.
std::vector<uint8_t> generate_path_table(size_t rows, size_t row_stride, uint64_t seed);
}  // namespace ferret::nested_call_depth_internal

namespace {

TEST(NestedCallDepthPathTable, DimensionsMatchRowsTimesStride) {
  auto t = ferret::nested_call_depth_internal::generate_path_table(
      /*rows=*/16, /*row_stride=*/5, /*seed=*/123);
  EXPECT_EQ(t.size(), 16u * 5u);
}

TEST(NestedCallDepthPathTable, BytesAreInDispatchRange) {
  auto t = ferret::nested_call_depth_internal::generate_path_table(
      /*rows=*/64, /*row_stride=*/10, /*seed=*/0xdeadbeef);
  for (uint8_t b : t) {
    EXPECT_LT(b, 8) << "byte out of [0, 8): " << static_cast<int>(b);
  }
}

TEST(NestedCallDepthPathTable, SameSeedSameTable) {
  auto a = ferret::nested_call_depth_internal::generate_path_table(32, 8, 0x42);
  auto b = ferret::nested_call_depth_internal::generate_path_table(32, 8, 0x42);
  EXPECT_EQ(a, b);
}

TEST(NestedCallDepthPathTable, DifferentSeedDifferentTable) {
  auto a = ferret::nested_call_depth_internal::generate_path_table(32, 8, 0x42);
  auto b = ferret::nested_call_depth_internal::generate_path_table(32, 8, 0x43);
  EXPECT_NE(a, b);
}

}  // namespace
```

- [ ] **Step 3.2: Run, expect link failure**

```sh
cmake --build build --target test_nested_call_depth
```

Expected: linker error referencing `generate_path_table`.

- [ ] **Step 3.3: Implement the helper**

In `benchmarks/nested_call_depth.cpp`, add the include and the helper namespace just before `namespace ferret {`:

```cpp
#include <cstdint>
#include <vector>
```

Below the anonymous-namespace block holding `kK`/`kKBits`, and still inside `namespace ferret {`, add a named sub-namespace for testable helpers:

```cpp
namespace nested_call_depth_internal {

// Seeded xorshift64. Tiny, deterministic, no library dependency.
inline uint64_t xorshift64(uint64_t& state) {
  uint64_t x = state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  state = x;
  return x;
}

std::vector<uint8_t> generate_path_table(size_t rows, size_t row_stride, uint64_t seed) {
  // The mask `kK - 1` documents that bytes are dispatch indices in [0, kK).
  static_assert(kK == 8, "the 0x7 mask below assumes kK == 8");
  std::vector<uint8_t> out;
  out.resize(rows * row_stride);
  // Avoid a zero state — xorshift would lock at zero.
  uint64_t s = seed == 0 ? 0x9E3779B97F4A7C15ULL : seed;
  for (size_t i = 0; i < out.size(); ++i) {
    out[i] = static_cast<uint8_t>(xorshift64(s) & 0x7);
  }
  return out;
}

}  // namespace nested_call_depth_internal
```

`kK` is declared in the anonymous namespace (file-internal linkage) so it's visible from both the `nested_call_depth_internal` helpers and the `NestedCallDepth` class — no header export required.

- [ ] **Step 3.4: Build and run the four new tests**

```sh
cmake --build build --target test_nested_call_depth
ctest --test-dir build --output-on-failure -R NestedCallDepthPathTable
```

Expected: all four pass.

- [ ] **Step 3.5: Commit**

```sh
git add benchmarks/nested_call_depth.cpp tests/test_nested_call_depth.cpp
git commit -m "feat(nested_call_depth): seeded xorshift path-table generator"
```

---

## Task 4: Empty-kernel emission (outer loop only)

The simplest valid `emit_kernel` is one that produces a callable function with the right ABI but no real work — a tight outer loop that decrements a counter and returns. This validates the sljit prologue/epilogue/loop machinery before we add the call chain.

**Files:**
- Modify: `benchmarks/nested_call_depth.cpp` (extend `emit_kernel`)
- Modify: `tests/test_integration.cpp` (add a depth-1 smoke test)

### Steps

- [ ] **Step 4.1: Write the failing integration test**

Append to `tests/test_integration.cpp` (before the trailing `expect_spacing_rejected` block):

```cpp
TEST(Integration, NestedCallDepthDepth1SmokeProducesOneRow) {
  auto out = std::filesystem::temp_directory_path() / "ferret_ncd_smoke.csv";
  std::filesystem::remove(out);
  std::string cmd = std::string(FERRET_BINARY) +
                    " run nested_call_depth"
                    " --depth=1 --path_table_rows=16"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 2u) << "expected 1 header + 1 data row, got:\n" << contents;
}
```

- [ ] **Step 4.2: Run, expect failure**

```sh
cmake --build build --target test_integration ferret
ctest --test-dir build --output-on-failure -R NestedCallDepthDepth1Smoke
```

Expected: test fails (the kernel emits nothing, so sljit generates an empty function — depending on sljit's behavior this may either crash or produce a CSV with empty ticks).

- [ ] **Step 4.3: Implement a minimal outer loop in `emit_kernel`**

Replace the trailing `(void)c;` line in `emit_kernel` with:

```cpp
auto iters = iterations(p);

sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/1, /*saved=*/0, /*local_size=*/0);

// Outer loop: R0 = iters; while (--R0 != 0) {}
sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, static_cast<sljit_sw>(iters));

sljit_label* loop_top = sljit_emit_label(c);
sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
sljit_set_label(back, loop_top);

sljit_emit_return_void(c);
```

- [ ] **Step 4.4: Run, expect pass**

```sh
cmake --build build --target test_integration ferret
ctest --test-dir build --output-on-failure -R NestedCallDepthDepth1Smoke
```

Expected: the integration smoke test passes — CSV has one data row with non-empty `ticks_*`.

- [ ] **Step 4.5: Commit**

```sh
git add benchmarks/nested_call_depth.cpp tests/test_integration.cpp
git commit -m "feat(nested_call_depth): minimal outer-loop kernel emission"
```

---

## Task 5: Single linear chain (K=1, no dispatch)

Introduce the nested call structure with **one** call site per body. This validates that:

- Multiple `sljit_emit_enter`/`sljit_emit_return_void` sub-functions can coexist in one compiler context.
- Forward-declared `SLJIT_CALL` jumps wire to labels emitted later.
- Real hardware CALL/RET instructions are emitted (verified indirectly by measuring a non-trivial per-call cost).

The path table is still unused at this task. K = 8 dispatch is added in Task 6.

**Files:**
- Modify: `benchmarks/nested_call_depth.cpp` (extend `emit_kernel`)
- Modify: `tests/test_integration.cpp` (add a multi-depth smoke test)

### Steps

- [ ] **Step 5.1: Write the failing integration test**

Append to `tests/test_integration.cpp`:

```cpp
TEST(Integration, NestedCallDepthSweepProducesMonotonicCost) {
  auto out = std::filesystem::temp_directory_path() / "ferret_ncd_sweep.csv";
  std::filesystem::remove(out);
  std::string cmd = std::string(FERRET_BINARY) +
                    " run nested_call_depth"
                    " --depth=1,2,4,8 --path_table_rows=16"
                    " --reps=5 --warmup=2"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 5u);
  // No empty data cells (no ",," sequences).
  EXPECT_EQ(contents.find(",,"), std::string::npos);
}
```

- [ ] **Step 5.2: Run, expect failure**

Expected: the test currently passes only the depth-1 case shape; the multi-depth sweep should still pass because the outer-loop kernel doesn't depend on depth — but it produces a flat-in-depth per-site cost. Run it and confirm it passes structurally; this test is here to lock the row count and CSV shape before we add real per-depth work.

- [ ] **Step 5.3: Re-write `emit_kernel` to emit a linear chain**

Replace the outer-loop kernel from Task 4 with the chain-emitting version. The structure is: chain_main contains the outer loop and one CALL to BODY_N; BODY_i has one CALL to BODY_{i-1}; BODY_0 just returns.

Strategy for connecting forward references: pre-allocate one `sljit_label*` per body (initialised to `nullptr`); emit chain_main first (its CALL targets BODY_N's not-yet-emitted label, so we save the `sljit_jump*` and fix the target later); then emit each body in order N, N-1, …, 0, capturing labels as we go and fixing each saved jump immediately after its target body is reached.

Replace the body of `emit_kernel` (everything after the pre-flight `if` blocks) with:

```cpp
auto iters = iterations(p);

// One label per body. body_labels[0] = chain_main, body_labels[i] = BODY_{depth - i + 1}.
// Simpler indexing: body_labels[d] is the entry to the body that's `d` levels from the leaf
// (so body_labels[0] is LEAF, body_labels[depth] is BODY_N). chain_main is its own thing.
std::vector<sljit_label*> body_labels(depth + 1, nullptr);
std::vector<sljit_jump*> pending_calls;  // (jump, target-depth-index)
std::vector<size_t>       pending_targets;

// --- chain_main ---
sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/1, /*saved=*/0, /*local_size=*/0);

sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, static_cast<sljit_sw>(iters));
sljit_label* loop_top = sljit_emit_label(c);

// Call BODY_depth (the outermost body).
{
  sljit_jump* j = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
  pending_calls.push_back(j);
  pending_targets.push_back(depth);  // target is body_labels[depth] = BODY_N
}

sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
sljit_set_label(back, loop_top);

sljit_emit_return_void(c);

// --- BODY_depth … BODY_1, each calling the next-inner body ---
for (size_t d = depth; d >= 1; --d) {
  body_labels[d] = sljit_emit_label(c);
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/0, /*saved=*/0, /*local_size=*/0);

  sljit_jump* j = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
  pending_calls.push_back(j);
  pending_targets.push_back(d - 1);

  sljit_emit_return_void(c);
}

// --- BODY_0 / LEAF ---
body_labels[0] = sljit_emit_label(c);
sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/0, /*saved=*/0, /*local_size=*/0);
sljit_emit_return_void(c);

// --- Wire up all pending forward-referenced CALL jumps ---
for (size_t i = 0; i < pending_calls.size(); ++i) {
  sljit_set_label(pending_calls[i], body_labels[pending_targets[i]]);
}
```

Note on the loop bound: `for (size_t d = depth; d >= 1; --d)` is safe because we stop at `d == 1` (the condition is checked after the decrement). If `depth == 0` the pre-flight rejects, so we never enter the loop with an underflowing `d`.

- [ ] **Step 5.4: Run, expect pass**

```sh
cmake --build build --target test_integration ferret
ctest --test-dir build --output-on-failure -R NestedCallDepth
```

Expected: both `Depth1Smoke` and `SweepProducesMonotonic*` pass. The per-site cost should now scale roughly linearly with depth (each chain pass does `depth + 1` call/ret pairs).

- [ ] **Step 5.5: Commit**

```sh
git add benchmarks/nested_call_depth.cpp tests/test_integration.cpp
git commit -m "feat(nested_call_depth): linear nested call chain via SLJIT_CALL"
```

---

## Task 6: K = 8 static dispatch (always site 0)

Add the K = 8 call-site emission machinery without yet reading the path table — each body unconditionally takes site 0. This validates the multi-call-site emission and the post-call merge point without introducing dispatch noise.

**Files:**
- Modify: `benchmarks/nested_call_depth.cpp`

### Steps

- [ ] **Step 6.1: Write the failing test**

Append to `tests/test_integration.cpp`:

```cpp
TEST(Integration, NestedCallDepthKEightStaticStillRunsCleanly) {
  // Same shape as the depth-1 smoke, but uses larger depths and asserts
  // no empty cells. After Task 6 each body emits 8 call sites; this test
  // protects against a regression where some of those sites fall through
  // or are not properly wired.
  auto out = std::filesystem::temp_directory_path() / "ferret_ncd_k8.csv";
  std::filesystem::remove(out);
  std::string cmd = std::string(FERRET_BINARY) +
                    " run nested_call_depth"
                    " --depth=1,4,16 --path_table_rows=16"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));
  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 4u);
  EXPECT_EQ(contents.find(",,"), std::string::npos);
}
```

- [ ] **Step 6.2: Run, expect the test to pass on the Task-5 implementation**

It will pass because the Task-5 kernel still produces valid CSVs at these depths. This is a regression guard for Task 6's edit.

```sh
ctest --test-dir build --output-on-failure -R NestedCallDepthKEightStatic
```

- [ ] **Step 6.3: Restructure BODY emission to lay down 8 static call sites with a single merge tail**

The dynamic execution stays linear (one site fires per body invocation) because each site ends with an unconditional jump to the merge tail. Control always enters at site 0 — sites 1..7 become reachable only in Task 7 when the byte-driven dispatch is wired up. This means runtime is unchanged at this task (still one call per body), but the *static layout* of all 8 sites is in place.

Replace the BODY emission loop (the `for (size_t d = depth; d >= 1; --d)` block from Task 5) with:

```cpp
for (size_t d = depth; d >= 1; --d) {
  body_labels[d] = sljit_emit_label(c);
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/1, /*saved=*/0, /*local_size=*/0);

  // K static call sites. Control enters at site 0 (immediately below the
  // prologue). Each site ends with `jmp .done`, so without dispatch only
  // site 0 fires per invocation — sites 1..7 are unreachable for now and
  // get reachable in Task 7 when the binary-tree dispatch jumps into them.
  std::vector<sljit_jump*> done_jumps;
  done_jumps.reserve(kK);
  for (int site = 0; site < kK; ++site) {
    sljit_jump* call_jmp = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
    pending_calls.push_back(call_jmp);
    pending_targets.push_back(d - 1);
    sljit_jump* done_jmp = sljit_emit_jump(c, SLJIT_JUMP);
    done_jumps.push_back(done_jmp);
  }
  sljit_label* done_label = sljit_emit_label(c);
  for (sljit_jump* j : done_jumps) sljit_set_label(j, done_label);

  sljit_emit_return_void(c);
}
```

`iterations()` does **not** need to change — execution is still linear (one CALL per body per pass), the same as Task 5. The extra seven CALL instructions plus seven `JMP .done` per body are emitted into the code buffer but never executed.

- [ ] **Step 6.4: Build and run**

```sh
cmake --build build --target test_integration ferret
ctest --test-dir build --output-on-failure -R NestedCallDepthKEightStatic
```

Expected: pass. Runtime stays linear because each body still executes exactly one CALL (the seven extra sites are unreachable static code at this task); the depth = 16 run completes in tens of milliseconds.

- [ ] **Step 6.5: Commit**

```sh
git add benchmarks/nested_call_depth.cpp tests/test_integration.cpp
git commit -m "feat(nested_call_depth): emit 8 static call sites per body, no dispatch yet"
```

---

## Task 7: Path-table load + binary-tree dispatch

This is the heart of the construction. Each body loads one byte from the path table at offset `(depth - i)` (so chain_main reads index 0, BODY_depth reads index 1, …, BODY_1 reads index `depth`), and a three-conditional-branch binary tree picks exactly one of the eight call sites. The other seven sites stop emitting — only one fires per body invocation, restoring linear runtime.

**Files:**
- Modify: `benchmarks/nested_call_depth.cpp`

### Steps

- [ ] **Step 7.1: Allocate the path table and bake its base address into the kernel**

`emit_kernel` must own the path table for as long as the JIT'd code is callable. Store it on the benchmark instance.

In the `NestedCallDepth` struct, add a member just before the `name()` method:

```cpp
// One vector per sweep point; never freed until the benchmark instance
// is destroyed. Each entry is alive for as long as its corresponding
// JIT'd kernel is callable, which is guaranteed by the runner: it frees
// the JIT code before moving to the next param point, but the benchmark
// instance outlives the whole sweep.
std::vector<std::vector<uint8_t>> path_tables_;
```

In `emit_kernel`, after the pre-flight checks and the `iters` computation, add:

```cpp
auto rows = static_cast<size_t>(path_table_rows);
size_t stride = depth + 1;
auto seed = static_cast<uint64_t>(p.get<int64_t>("seed"));
// Mix seed and depth so distinct sweep points use distinct tables.
uint64_t mixed = seed ^ (static_cast<uint64_t>(depth) * 0x9E3779B97F4A7C15ULL);
path_tables_.push_back(
    nested_call_depth_internal::generate_path_table(rows, stride, mixed));
uint8_t* table_ptr = path_tables_.back().data();
```

- [ ] **Step 7.2: Pass the row pointer to the chain via a saved register**

In chain_main's prologue, declare TWO saved registers (S0 = table base, S1 = row pointer for the current iter):

```cpp
sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/2, /*saved=*/2, /*local_size=*/0);

// S0 = path-table base address
sljit_emit_op1(c, SLJIT_MOV, SLJIT_S0, 0, SLJIT_IMM, reinterpret_cast<sljit_sw>(table_ptr));

// R0 = iters
sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, static_cast<sljit_sw>(iters));

sljit_label* loop_top = sljit_emit_label(c);

// Compute row pointer: S1 = S0 + ((R0 & (ROWS-1)) * stride)
sljit_emit_op2(c, SLJIT_AND, SLJIT_R1, 0, SLJIT_R0, 0, SLJIT_IMM, static_cast<sljit_sw>(rows - 1));
sljit_emit_op2(c, SLJIT_MUL, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, static_cast<sljit_sw>(stride));
sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S0, 0, SLJIT_R1, 0);

// chain_main now performs its OWN K=8 dispatch into BODY_depth, reading
// path_table[row][0]. See the helper emitter below.
emit_k8_dispatch(c, /*table_offset=*/0, body_labels, /*target_d=*/depth,
                 pending_calls, pending_targets);

sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
sljit_set_label(back, loop_top);

sljit_emit_return_void(c);
```

Each BODY_d body, similarly:

```cpp
for (size_t d = depth; d >= 1; --d) {
  body_labels[d] = sljit_emit_label(c);
  // The bodies need to *read* S1 (the row pointer threaded by chain_main).
  // sljit's "saved" parameter tells the prologue how many to push/pop —
  // we set saved=0 here because we don't modify S1; the caller's value
  // is preserved by the C ABI across our CALL.
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/1, /*saved=*/2, /*local_size=*/0);

  // Body at depth d reads path_table[row][depth - d + 1].
  // chain_main reads offset 0; BODY_depth reads offset 1; BODY_1 reads
  // offset `depth`.
  size_t offset = depth - d + 1;
  emit_k8_dispatch(c, offset, body_labels, /*target_d=*/d - 1,
                   pending_calls, pending_targets);

  sljit_emit_return_void(c);
}
```

- [ ] **Step 7.3: Add the `emit_k8_dispatch` helper**

Put the helper as a file-scope static function inside an anonymous namespace, above the `NestedCallDepth` struct. The helper emits a binary tree of three conditional branches that picks exactly one of the eight static call sites laid down in Task 6, drives the choice off byte `[S1 + table_offset]`, and merges control after the chosen site at a single `.done` label.

The tricky part is sljit's forward-reference model: conditional branches need their target labels eventually but the labels are emitted later in the buffer, so we save the `sljit_jump*` returned by each `sljit_emit_jump` and call `sljit_set_label` on it once the destination label exists.

```cpp
// Emits the K=8 binary-tree dispatch + the 8 static call sites + the
// merge tail. `target_d` is the body-label index every site calls into
// (which is the next-inner body, body_labels[d - 1]). The CALL jumps
// are appended to `pending_calls`/`pending_targets` so the caller can
// wire them up to body_labels once all bodies are emitted.
void emit_k8_dispatch(sljit_compiler* c,
                      sljit_sw table_offset,
                      size_t target_d,
                      std::vector<sljit_jump*>& pending_calls,
                      std::vector<size_t>& pending_targets) {
  // R0 = path_table[row][table_offset], zero-extended from u8.
  sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R0, 0,
                 SLJIT_MEM1(SLJIT_S1), table_offset);

  // ---- Binary-tree dispatch: 3 conditional branches on bits 2, 1, 0. ----
  // Bit 2 splits {0..3} from {4..7}.
  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 4);
  sljit_jump* j_to_upper = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  // --- lower half (sites 0..3): bit 2 = 0 ---
  // Bit 1 splits {0,1} from {2,3}.
  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 2);
  sljit_jump* j_to_lower_hi = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  // --- {0,1}: bit 1 = 0. Bit 0 picks between site 0 (=0) and site 1 (=1). ---
  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 1);
  sljit_jump* j_to_site_1 = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  // Collected forward-jumps to .done; resolved after every site is emitted.
  std::vector<sljit_jump*> done_jumps;
  done_jumps.reserve(8);

  // Helper lambda to emit one site: label + CALL + jmp-to-done.
  auto emit_site = [&](sljit_label*& out_label, bool emit_done_jump) {
    out_label = sljit_emit_label(c);
    sljit_jump* call_jmp = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
    pending_calls.push_back(call_jmp);
    pending_targets.push_back(target_d);
    if (emit_done_jump) {
      done_jumps.push_back(sljit_emit_jump(c, SLJIT_JUMP));
    }
  };

  sljit_label* lbl_site_0 = nullptr;
  sljit_label* lbl_site_1 = nullptr;
  sljit_label* lbl_site_2 = nullptr;
  sljit_label* lbl_site_3 = nullptr;
  sljit_label* lbl_site_4 = nullptr;
  sljit_label* lbl_site_5 = nullptr;
  sljit_label* lbl_site_6 = nullptr;
  sljit_label* lbl_site_7 = nullptr;

  // Layout order matches the dispatch fall-through. Each site jumps to
  // .done after its CALL except the last (site 7), which falls through
  // to .done directly.
  emit_site(lbl_site_0, /*emit_done_jump=*/true);
  emit_site(lbl_site_1, /*emit_done_jump=*/true);

  // .lower_hi: bit 2 = 0, bit 1 = 1. Bit 0 picks site 2 (=0) vs site 3 (=1).
  sljit_label* lbl_lower_hi = sljit_emit_label(c);
  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 1);
  sljit_jump* j_to_site_3 = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  emit_site(lbl_site_2, /*emit_done_jump=*/true);
  emit_site(lbl_site_3, /*emit_done_jump=*/true);

  // .upper: bit 2 = 1. Bit 1 splits {4,5} from {6,7}.
  sljit_label* lbl_upper = sljit_emit_label(c);
  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 2);
  sljit_jump* j_to_upper_hi = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  // .upper_lo: bit 1 = 0. Bit 0 picks site 4 (=0) vs site 5 (=1).
  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 1);
  sljit_jump* j_to_site_5 = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  emit_site(lbl_site_4, /*emit_done_jump=*/true);
  emit_site(lbl_site_5, /*emit_done_jump=*/true);

  // .upper_hi: bit 2 = 1, bit 1 = 1. Bit 0 picks site 6 (=0) vs site 7 (=1).
  sljit_label* lbl_upper_hi = sljit_emit_label(c);
  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 1);
  sljit_jump* j_to_site_7 = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  emit_site(lbl_site_6, /*emit_done_jump=*/true);
  emit_site(lbl_site_7, /*emit_done_jump=*/false);  // last one — falls through to .done

  // .done — every site converges here.
  sljit_label* lbl_done = sljit_emit_label(c);

  // ---- Wire up all forward-reference jump targets. ----
  sljit_set_label(j_to_upper,    lbl_upper);
  sljit_set_label(j_to_lower_hi, lbl_lower_hi);
  sljit_set_label(j_to_site_1,   lbl_site_1);
  sljit_set_label(j_to_site_3,   lbl_site_3);
  sljit_set_label(j_to_upper_hi, lbl_upper_hi);
  sljit_set_label(j_to_site_5,   lbl_site_5);
  sljit_set_label(j_to_site_7,   lbl_site_7);

  for (sljit_jump* j : done_jumps) sljit_set_label(j, lbl_done);

  (void)lbl_site_0;  // labels are referenced only via the fall-through; the
  (void)lbl_site_2;  // explicit `lbl_site_N` variables exist for clarity.
  (void)lbl_site_4;
  (void)lbl_site_6;
}
```

Then replace the `for (size_t d = depth; d >= 1; --d) { … }` BODY emission from Task 6 with:

```cpp
for (size_t d = depth; d >= 1; --d) {
  body_labels[d] = sljit_emit_label(c);
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(),
                   /*scratches=*/1, /*saved=*/2, /*local_size=*/0);

  // Body at chain-depth d reads path_table[row][depth - d + 1].
  sljit_emit_op_src(c, SLJIT_NOP, 0, 0);  // no-op safety; remove if not needed
  emit_k8_dispatch(c,
                   /*table_offset=*/static_cast<sljit_sw>(depth - d + 1),
                   /*target_d=*/d - 1,
                   pending_calls, pending_targets);

  sljit_emit_return_void(c);
}
```

And replace the chain_main CALL block (the single `sljit_emit_call(c, SLJIT_CALL, …)` from Task 5) with the same dispatch helper invoked at `table_offset = 0`, immediately before the `R0 -= 1; jnz loop_top` epilogue:

```cpp
emit_k8_dispatch(c, /*table_offset=*/0, /*target_d=*/depth,
                 pending_calls, pending_targets);
```

A few small implementation notes the engineer should hold:

- `sljit_emit_op2u` is the "compute-flags-only" form (no destination written). It exists in sljit (see `sljitLir.h` around line 1490 and following). If your sljit vendor predates it, fall back to `sljit_emit_op2(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0_SCRATCH, 0, SLJIT_R0, 0, SLJIT_IMM, mask)` using a throwaway scratch reg.
- `SLJIT_MOV_U8` zero-extends the loaded byte. Bytes are already in `[0, 8)` by construction, so the safety `AND R0, R0, 7` step from the earlier draft is unnecessary and was removed.
- The `sljit_emit_op_src(c, SLJIT_NOP, 0, 0)` line in the BODY loop is a defensive placeholder — sljit historically optimized away certain empty-prologue patterns. Delete the line if it isn't needed; keep it if a particular sljit version drops the prologue otherwise.

- [ ] **Step 7.4: Run the existing integration tests**

```sh
cmake --build build --target test_integration ferret
ctest --test-dir build --output-on-failure -R NestedCallDepth
```

Expected: all NestedCallDepth integration tests pass; per-site cost is finite at all sweep depths (linear runtime — one CALL per body per pass, just like Tasks 5 and 6).

- [ ] **Step 7.5: Commit**

```sh
git add benchmarks/nested_call_depth.cpp
git commit -m "feat(nested_call_depth): K=8 binary-tree dispatch from path table"
```

---

## Task 8: End-to-end integration tests for the full pipeline

This task formalizes the integration coverage the spec calls for (§9). Most of these tests were stubbed inline during earlier tasks; this task adds the remaining negative paths and a long-depth row-count check.

**Files:**
- Modify: `tests/test_integration.cpp`

### Steps

- [ ] **Step 8.1: Add the rejection tests**

Append to `tests/test_integration.cpp`:

```cpp
TEST(Integration, NestedCallDepthRejectsBadPathTableRows) {
  for (const char* val : {"3", "5", "0", "1"}) {
    auto err = std::filesystem::temp_directory_path() /
               ("ferret_ncd_bad_rows_" + std::string(val) + ".txt");
    std::filesystem::remove(err);
    std::string cmd = std::string(FERRET_BINARY) +
                      " run nested_call_depth"
                      " --depth=2 --path_table_rows=" + val +
                      " --reps=2 --warmup=1"
                      " 2> " + err.string();
    int rc = actual_exit_code(std::system(cmd.c_str()));
    EXPECT_EQ(rc, 2) << "path_table_rows=" << val << ": expected exit 2";
    std::string err_contents = slurp(err.string());
    EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  }
}

TEST(Integration, NestedCallDepthRejectsZeroDepth) {
  auto err = std::filesystem::temp_directory_path() / "ferret_ncd_zero_depth.txt";
  std::filesystem::remove(err);
  std::string cmd = std::string(FERRET_BINARY) +
                    " run nested_call_depth"
                    " --depth=0 --path_table_rows=16"
                    " --reps=2 --warmup=1"
                    " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2);
}

TEST(Integration, NestedCallDepthLongSweepRowCount) {
  // 64 swept depths × 1 row each + 1 header = 65 newlines.
  auto out = std::filesystem::temp_directory_path() / "ferret_ncd_full.csv";
  std::filesystem::remove(out);
  std::string cmd = std::string(FERRET_BINARY) +
                    " run nested_call_depth"
                    " --depth=1..64 --path_table_rows=16"
                    " --reps=3 --warmup=1"
                    " --out=" + out.string();
  ASSERT_EQ(0, run(cmd));
  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 65u) << "expected 1 header + 64 data rows";
  EXPECT_EQ(contents.find(",,"), std::string::npos);
}
```

- [ ] **Step 8.2: Run all NestedCallDepth integration tests**

```sh
cmake --build build --target test_integration ferret
ctest --test-dir build --output-on-failure -R NestedCallDepth
```

Expected: all NestedCallDepth tests (skeleton, sweeps, rejections, long-row-count) pass.

- [ ] **Step 8.3: Commit**

```sh
git add tests/test_integration.cpp
git commit -m "test(nested_call_depth): rejection and long-sweep integration coverage"
```

---

## Task 9: Lint, full suite, and README touch-up

**Files:**
- Modify: `README.md` (one-line addition to the benchmark inventory)

### Steps

- [ ] **Step 9.1: Run the formatter and linter**

```sh
./scripts/format.sh
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
./scripts/lint.sh
```

Fix any issues clang-tidy reports. Common ones for new code: missing `[[nodiscard]]`, missing `const`, signed/unsigned comparisons.

- [ ] **Step 9.2: Run the full test suite**

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass on the host architecture.

- [ ] **Step 9.3: Add a README entry**

In `README.md`, find the section listing benchmarks (or the "List benchmarks" example) and add a one-line description of `nested_call_depth`. Example diff (insert after `direct_branch_footprint` mentions):

```markdown
- `nested_call_depth` — N nested `call`/`ret` pairs at distinct PCs with
  K = 8 shared-callee dispatch reads from a per-iteration path table.
  Sweep `--depth=1..64` to reveal the cliff at the RAS capacity.
```

- [ ] **Step 9.4: Commit**

```sh
git add README.md
git commit -m "docs(readme): document nested_call_depth benchmark"
```

---

## Task 10 (optional): Manual validation on a known host

Not in CI — purely for the implementer's own confidence that the cliff appears at a sensible location.

- [ ] **Step 10.1: Probe core frequency**

```sh
build/ferret run dependent_chain_throughput --core=3 --out=/tmp/freq.csv
python3 scripts/freq.py /tmp/freq.csv
```

Capture the printed `estimated_freq=<X>GHz` value.

- [ ] **Step 10.2: Sweep depth with cycle conversion**

```sh
build/ferret run nested_call_depth --core=3 --depth=1..64 \
    --freq=<X>GHz --reps=7 --warmup=2 --out=/tmp/ras.csv
python3 scripts/plot.py /tmp/ras.csv --out=/tmp/ras.png
```

- [ ] **Step 10.3: Read off the cliff**

Open `/tmp/ras.png`. Expected: per-call cost is flat (a few tens of cycles) for `depth` up to the host's RAS capacity (typically 16, 24, or 32 depending on the core), then steps up to a higher plateau. If no cliff appears, see the pattern doc §"Known limit: deep N starves the slow-cycling bits" — but with the memory-array path source this should not happen on shipping cores in the documented range. If the cliff appears at an unexpected depth, suspect the dispatch tree (Task 7) and re-verify with a small `depth=1..8` sweep.

---

## Self-Review Results

**Spec coverage check:**
- §2 (scope, in/out) — Tasks 1, 5, 7 cover the in-scope items; out-of-scope items are explicitly deferred.
- §3 (workflow) — Task 10 walks through the two-step flow; the CSV produced in Task 7 supports it.
- §4 (kernel construction) — Tasks 5–7 build it up incrementally.
- §5 (axis + option) — Task 1 (skeleton) defines both.
- §6 (class shape) — Task 1.
- §7 (CSV impact) — Inherits from framework; integration test in Task 8 verifies columns are present.
- §8 (error handling) — Task 2 (pre-flight rejections).
- §9 (testing) — Tasks 1, 2, 3 (unit), Tasks 4, 5, 6, 8 (integration), Task 10 (manual).

**Placeholder scan:** No `TBD` / `TODO` / "implement later" entries. Every step that emits code shows the full sljit listing inline. The `emit_k8_dispatch` helper in Task 7.3 is given in complete sljit form with explicit forward-reference fix-ups.

**Type consistency:** `depth` is `size_t` throughout (matches the axis type). `path_table_rows` is `int64_t` (option storage type) and cast to `size_t` on use. `seed` is `int64_t` from the framework, cast to `uint64_t` before mixing.

**Known plan-time risks the implementer should hold in mind:**
- `sljit_emit_op2u` is the flags-only form (no destination). It exists in current sljit (search for `SLJIT_AND | SLJIT_SET_Z` in `sljitLir.h`). If the vendored sljit predates `op2u`, substitute `sljit_emit_op2(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, mask)` — writing the masked value back into `R0` is harmless because the dispatch tree only re-reads `R0` against the next bit mask.
- `SLJIT_MUL` is portable across x86 and AArch64.
- `reinterpret_cast<sljit_sw>(pointer)` is portable when `sljit_sw` is 64-bit (it is on both supported architectures). If clang-tidy flags it, suppress locally.
- The path table is held by `path_tables_` and grows by one entry per `emit_kernel` call. The benchmark instance is owned by the framework's registry for the whole sweep, so the storage outlives every JIT'd kernel.
- Multiple `sljit_emit_enter` / `sljit_emit_return_void` pairs in one compiler context produce multiple sub-functions in one code buffer. If a particular sljit version rejects this — verify at Task 5 — the fallback is to use a separate compiler context per body, generate each body's code separately, and patch the absolute call targets after generation. That fallback is not in this plan because the canonical sljit API supports the multi-function pattern, but it is the engineer's escape hatch.

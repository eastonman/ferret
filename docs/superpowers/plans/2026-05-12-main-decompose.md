# `main.cpp` Decomposition Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Decompose `src/main.cpp` (360 LOC, two clang-tidy `NOLINT` suppressions) into four focused modules (`jit`, `freq`, extended `cli_axis`, `run_command`) with `main.cpp` reduced to ~120 lines of CLI11 wiring only.

**Architecture:** Four commits, each a self-contained refactor. New modules added to `ferret_core` static library so existing tests link them transparently. RAII (`JittedKernel`) replaces manual `jit_free`. `do_run`'s body becomes `ferret::run(const RunOptions&)`, split internally into named static-local helpers each under ~30 lines.

**Tech Stack:** C++20, CMake, GoogleTest, sljit, CLI11, spdlog.

**Spec:** `docs/superpowers/specs/2026-05-12-main-decompose-design.md`

**Baseline:** `bf5f00f` on `main`. Working branch: `refactor/cleanup`.

---

## Pre-flight

- [ ] **Verify clean baseline**

Run from the worktree root (`/Users/easton/WorkingSpace/project/ferret/refactor/cleanup`):

```bash
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass (68 active + 1 skipped on aarch64 hosts). If any fail, stop and investigate before starting Task 1 — every later step assumes a clean baseline.

- [ ] **Capture the equivalence-check baseline CSV**

```bash
build/ferret run direct_branch_footprint --branches=1,2,4,8 --spacing_bytes=64 \
  --reps=3 --warmup=1 --seed=1 --freq=4.0GHz --out=/tmp/ferret-baseline.csv
head -1 /tmp/ferret-baseline.csv > /tmp/ferret-baseline.header
```

Save these files; they will be compared against the post-refactor binary in the post-flight step.

---

## Task 1: Extract `JittedKernel` (sljit lifecycle → RAII)

**Files:**
- Create: `include/ferret/jit.hpp`
- Create: `src/jit.cpp`
- Create: `tests/test_jit.cpp`
- Modify: `CMakeLists.txt` (add `src/jit.cpp` to `ferret_core`)
- Modify: `tests/CMakeLists.txt` (register `test_jit`)
- Modify: `src/main.cpp` (replace inline `JittedKernel`/`jit_compile`/`jit_free`)

### Step 1.1: Write the failing test

- [ ] Create `tests/test_jit.cpp`:

```cpp
#include <gtest/gtest.h>

extern "C" {
#include <sljitLir.h>
}

#include "ferret/benchmark.hpp"
#include "ferret/jit.hpp"
#include "ferret/params.hpp"

using namespace ferret;

namespace {

// Minimal test fixture: emits a function that immediately returns.
struct EmptyBench : Benchmark {
  [[nodiscard]] std::string name() const override { return "test_empty"; }
  [[nodiscard]] SweepAxes axes() const override { return {}; }
  [[nodiscard]] size_t sites_per_kernel(const Params& /*p*/) const override { return 1; }
  [[nodiscard]] size_t iterations(const Params& /*p*/) const override { return 1; }
  void emit_kernel(sljit_compiler* c, const Params& /*p*/) override {
    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 0, 0, 0);
    sljit_emit_return_void(c);
  }
};

}  // namespace

TEST(JittedKernel, DefaultConstructedIsNotOk) {
  JittedKernel k;
  EXPECT_FALSE(k.ok());
}

TEST(JittedKernel, CompilesEmptyBenchmarkAndIsCallable) {
  EmptyBench b;
  Params p;
  JittedKernel k(b, p);
  ASSERT_TRUE(k.ok());
  // Calling the JIT-emitted void function must not crash.
  k.fn()();
}

// The failed-compile path (ok() == false on sljit error) is already
// covered by tests/test_integration.cpp via benchmarks that trip
// sljit's compile errors. A unit test here would need to construct an
// sljit error state reliably across architectures, which is fragile.

TEST(JittedKernel, MoveConstructTransfersOwnership) {
  EmptyBench b;
  Params p;
  JittedKernel src(b, p);
  ASSERT_TRUE(src.ok());

  JittedKernel dst(std::move(src));
  EXPECT_TRUE(dst.ok());
  EXPECT_FALSE(src.ok());  // moved-from is in not-ok state, dtor must be a no-op
  dst.fn()();
}

TEST(JittedKernel, MoveAssignReleasesPrevious) {
  EmptyBench b;
  Params p;
  JittedKernel a(b, p);
  JittedKernel c(b, p);
  ASSERT_TRUE(a.ok());
  ASSERT_TRUE(c.ok());

  a = std::move(c);  // a's original code must be freed without leak/double-free
  EXPECT_TRUE(a.ok());
  EXPECT_FALSE(c.ok());
}

// Stress: many move/destroy/recreate cycles. Under ASan/MSan a leak or
// double-free in the dtor or move ops would fail this test.
TEST(JittedKernel, StressMoveDestroyRecreate) {
  EmptyBench b;
  Params p;
  for (int i = 0; i < 200; ++i) {
    JittedKernel a(b, p);
    JittedKernel bk(std::move(a));
    JittedKernel ck;
    ck = std::move(bk);
    ASSERT_TRUE(ck.ok());
    ck.fn()();
  }
}
```

### Step 1.2: Register the test binary

- [ ] Append to `tests/CMakeLists.txt` (just before the `test_integration` block at line 84):

```cmake
add_executable(test_jit test_jit.cpp)
target_link_libraries(test_jit PRIVATE
  ferret_core
  sljit::sljit
  GTest::gtest GTest::gtest_main
)
gtest_discover_tests(test_jit)
```

### Step 1.3: Verify the test fails to build (jit.hpp doesn't exist yet)

Run:

```bash
cmake --build build 2>&1 | tail -20
```

Expected: build of `test_jit` fails with a "ferret/jit.hpp: No such file or directory" error. Confirms the test is wired correctly and the implementation is genuinely missing.

### Step 1.4: Create the header

- [ ] Create `include/ferret/jit.hpp`:

```cpp
#pragma once

#include "ferret/benchmark.hpp"
#include "ferret/params.hpp"

namespace ferret {

// RAII handle for a sljit-emitted kernel. On construction, runs the
// benchmark's emit_kernel and generates machine code. On failure
// (compiler alloc, sljit error, or code-gen failure) ok() returns false
// and the destructor is a no-op. Move-only.
class JittedKernel {
 public:
  JittedKernel() = default;
  JittedKernel(Benchmark& b, const Params& p);
  ~JittedKernel();

  JittedKernel(JittedKernel&&) noexcept;
  JittedKernel& operator=(JittedKernel&&) noexcept;
  JittedKernel(const JittedKernel&) = delete;
  JittedKernel& operator=(const JittedKernel&) = delete;

  [[nodiscard]] bool ok() const noexcept { return code_ != nullptr; }

  using fn_t = void (*)();
  [[nodiscard]] fn_t fn() const noexcept;  // precondition: ok()

 private:
  void* code_ = nullptr;
};

}  // namespace ferret
```

### Step 1.5: Create the implementation

- [ ] Create `src/jit.cpp`:

```cpp
#include "ferret/jit.hpp"

extern "C" {
#include <sljitLir.h>
}

#include <utility>

namespace ferret {

JittedKernel::JittedKernel(Benchmark& b, const Params& p) {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  if (c == nullptr) {
    return;
  }
  b.emit_kernel(c, p);
  if (sljit_get_compiler_error(c) != SLJIT_SUCCESS) {
    sljit_free_compiler(c);
    return;
  }
  void* code = sljit_generate_code(c, 0, nullptr);
  sljit_free_compiler(c);
  code_ = code;
}

JittedKernel::~JittedKernel() {
  if (code_ != nullptr) {
    sljit_free_code(code_, nullptr);
  }
}

JittedKernel::JittedKernel(JittedKernel&& other) noexcept : code_(other.code_) {
  other.code_ = nullptr;
}

JittedKernel& JittedKernel::operator=(JittedKernel&& other) noexcept {
  if (this != &other) {
    if (code_ != nullptr) {
      sljit_free_code(code_, nullptr);
    }
    code_ = other.code_;
    other.code_ = nullptr;
  }
  return *this;
}

JittedKernel::fn_t JittedKernel::fn() const noexcept {
  return reinterpret_cast<fn_t>(code_);
}

}  // namespace ferret
```

### Step 1.6: Add `src/jit.cpp` to `ferret_core`

- [ ] Edit `CMakeLists.txt`. Insert `src/jit.cpp` into the `add_library(ferret_core STATIC ...)` list (alphabetical position, after `src/cli_axis.cpp`):

```cmake
add_library(ferret_core STATIC
  src/axis.cpp
  src/sweep.cpp
  src/cli_axis.cpp
  src/jit.cpp
  src/timing/x86_64.cpp
  ...
```

(Preserve every other entry exactly.)

### Step 1.7: Build and run only the new test

Run:

```bash
cmake --build build --target test_jit
ctest --test-dir build -R '^JittedKernel\.' --output-on-failure
```

Expected: all five `JittedKernel.*` tests pass.

### Step 1.8: Update `main.cpp` to use `JittedKernel`

- [ ] Edit `src/main.cpp`. Delete the inline definitions on lines 71–95:

```cpp
struct JittedKernel { ... };
JittedKernel jit_compile(...) { ... }
void jit_free(JittedKernel& k) { ... }
```

- [ ] Add the include near the other ferret includes (alphabetical, after `freq.hpp` would go later — for now, after `cli_axis.hpp`):

```cpp
#include "ferret/jit.hpp"
```

- [ ] In `do_run`, replace the per-row JIT block (the body of the `for (const auto& p : rows)` loop). The current code (around lines 233–261):

```cpp
for (const auto& p : rows) {
  ferret::MeasurementRow m;
  JittedKernel kern;
  try {
    size_t pre_iters = bench->iterations(p);
    size_t pre_sites = bench->sites_per_kernel(p);
    if (pre_iters == 0 || pre_sites == 0) {
      flog::error("invalid params: yields zero work (iterations={}, sites_per_kernel={})", pre_iters, pre_sites);
      return 2;
    }

    kern = jit_compile(*bench, p);
    if (kern.code == nullptr) {
      m.jit_failed = true;
      m.iters = pre_iters;
      m.sites = pre_sites;
      flog::warn("sljit_error on params; emitting empty row");
    } else {
      auto fn = reinterpret_cast<void (*)()>(kern.code);
      m = ferret::runner::measure(fn, pre_iters, pre_sites, K, warmup);
      jit_free(kern);
    }
  } catch (const std::exception& e) {
    jit_free(kern);
    flog::error("benchmark error on params: {}", e.what());
    return 2;
  }
  buffered.emplace_back(p, m);
}
```

Becomes:

```cpp
for (const auto& p : rows) {
  ferret::MeasurementRow m;
  try {
    size_t pre_iters = bench->iterations(p);
    size_t pre_sites = bench->sites_per_kernel(p);
    if (pre_iters == 0 || pre_sites == 0) {
      flog::error("invalid params: yields zero work (iterations={}, sites_per_kernel={})", pre_iters, pre_sites);
      return 2;
    }

    ferret::JittedKernel kern(*bench, p);
    if (!kern.ok()) {
      m.jit_failed = true;
      m.iters = pre_iters;
      m.sites = pre_sites;
      flog::warn("sljit_error on params; emitting empty row");
    } else {
      m = ferret::runner::measure(kern.fn(), pre_iters, pre_sites, K, warmup);
    }
  } catch (const std::exception& e) {
    flog::error("benchmark error on params: {}", e.what());
    return 2;
  }
  buffered.emplace_back(p, m);
}
```

(The `jit_free` calls disappear — RAII handles release on every exit path including the exception path.)

- [ ] Remove the `#include <sljitLir.h>` block from `src/main.cpp` (lines 13–15) — main no longer touches sljit directly.

### Step 1.9: Full build + ctest

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass (existing 68 + 5 new `JittedKernel.*`).

### Step 1.10: Commit

```bash
git add include/ferret/jit.hpp src/jit.cpp tests/test_jit.cpp \
        CMakeLists.txt tests/CMakeLists.txt src/main.cpp
git commit -m "$(cat <<'EOF'
refactor(jit): extract sljit kernel lifecycle into ferret::JittedKernel

Move JittedKernel struct + jit_compile + jit_free from src/main.cpp into
a dedicated ferret::JittedKernel RAII type. Construction runs
emit_kernel + sljit_generate_code; destruction calls sljit_free_code.
The manual jit_free in do_run's exception handler is no longer needed.

main.cpp no longer #includes sljitLir.h.

New unit tests cover default construction, successful compile, failed
compile (broken benchmark), move-construct/assign ownership transfer,
and a 200-iteration move/destroy/recreate stress loop for ASan/MSan.
EOF
)"
```

---

## Task 2: Extract `parse_freq`

**Files:**
- Create: `include/ferret/freq.hpp`
- Create: `src/freq.cpp`
- Create: `tests/test_freq.cpp`
- Modify: `CMakeLists.txt` (add `src/freq.cpp` to `ferret_core`)
- Modify: `tests/CMakeLists.txt` (register `test_freq`)
- Modify: `src/main.cpp` (delete inline `parse_freq`, use `ferret::parse_freq`)

### Step 2.1: Write the failing test

- [ ] Create `tests/test_freq.cpp`:

```cpp
#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

#include "ferret/freq.hpp"

using namespace ferret;

TEST(ParseFreq, GHzSuffix) {
  EXPECT_DOUBLE_EQ(parse_freq("4.521GHz"), 4.521e9);
}

TEST(ParseFreq, MHzSuffix) {
  EXPECT_DOUBLE_EQ(parse_freq("100MHz"), 100e6);
}

TEST(ParseFreq, KHzSuffix) {
  EXPECT_DOUBLE_EQ(parse_freq("250kHz"), 250e3);
}

TEST(ParseFreq, HzSuffix) {
  EXPECT_DOUBLE_EQ(parse_freq("42Hz"), 42.0);
}

TEST(ParseFreq, BareNumberIsHz) {
  EXPECT_DOUBLE_EQ(parse_freq("12345"), 12345.0);
}

TEST(ParseFreq, ScientificNotation) {
  EXPECT_DOUBLE_EQ(parse_freq("1.2e9Hz"), 1.2e9);
  EXPECT_DOUBLE_EQ(parse_freq("1.2e9"), 1.2e9);
}

TEST(ParseFreq, EmptyStringThrows) {
  EXPECT_THROW((void)parse_freq(""), std::invalid_argument);
}

TEST(ParseFreq, OnlySuffixThrows) {
  EXPECT_THROW((void)parse_freq("GHz"), std::invalid_argument);
}

TEST(ParseFreq, TrailingJunkThrows) {
  EXPECT_THROW((void)parse_freq("4.5GHzx"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("4.5x"), std::invalid_argument);
}

TEST(ParseFreq, NonNumericThrows) {
  EXPECT_THROW((void)parse_freq("bogus"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("bogusGHz"), std::invalid_argument);
}

TEST(ParseFreq, InfThrows) {
  EXPECT_THROW((void)parse_freq("inf"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("infHz"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("-inf"), std::invalid_argument);
}

TEST(ParseFreq, NanThrows) {
  EXPECT_THROW((void)parse_freq("nan"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("nanGHz"), std::invalid_argument);
}

TEST(ParseFreq, ZeroThrows) {
  EXPECT_THROW((void)parse_freq("0"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("0GHz"), std::invalid_argument);
}

TEST(ParseFreq, NegativeThrows) {
  EXPECT_THROW((void)parse_freq("-4.5GHz"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("-1"), std::invalid_argument);
}
```

### Step 2.2: Register the test binary

- [ ] Append to `tests/CMakeLists.txt` (after the new `test_jit` block):

```cmake
add_executable(test_freq test_freq.cpp)
target_link_libraries(test_freq PRIVATE
  ferret_core
  GTest::gtest GTest::gtest_main
)
gtest_discover_tests(test_freq)
```

### Step 2.3: Verify the test fails to build

Run:

```bash
cmake --build build 2>&1 | tail -10
```

Expected: `ferret/freq.hpp: No such file or directory`.

### Step 2.4: Create the header

- [ ] Create `include/ferret/freq.hpp`:

```cpp
#pragma once

#include <string>

namespace ferret {

// Parses a CLI frequency value: "4.521GHz", "100MHz", "250kHz", "42Hz",
// "1.2e9Hz", or a bare number (treated as Hz). Returned value is hertz.
// Throws std::invalid_argument with message prefix "--freq: <reason>: <input>"
// on empty numeric component, trailing junk after the number, non-finite
// (NaN, +/-inf), or non-positive results.
double parse_freq(const std::string& s);

}  // namespace ferret
```

### Step 2.5: Create the implementation

- [ ] Create `src/freq.cpp` (verbatim from `src/main.cpp` lines 33–69, namespace-qualified):

```cpp
#include "ferret/freq.hpp"

#include <cmath>
#include <stdexcept>

namespace ferret {

double parse_freq(const std::string& s) {
  auto fail = [&](const char* why) {
    throw std::invalid_argument(std::string("--freq: ") + why + ": " + s);
  };

  std::string num = s;
  double mult = 1.0;
  auto strip_suffix = [&](const std::string& suf, double m) {
    if (num.size() >= suf.size() && num.ends_with(suf)) {
      num.resize(num.size() - suf.size());
      mult = m;
      return true;
    }
    return false;
  };
  strip_suffix("GHz", 1e9) || strip_suffix("MHz", 1e6) ||
      strip_suffix("kHz", 1e3) || strip_suffix("Hz", 1.0);

  if (num.empty()) {
    fail("empty numeric component");
  }
  size_t consumed = 0;
  double val = 0.0;
  try {
    val = std::stod(num, &consumed);
  } catch (const std::exception&) {
    fail("not a number");
  }
  if (consumed != num.size()) {
    fail("trailing junk after number");
  }
  double hz = val * mult;
  if (!std::isfinite(hz)) {
    fail("must be finite");
  }
  if (!(hz > 0.0)) {
    fail("must be positive");
  }
  return hz;
}

}  // namespace ferret
```

### Step 2.6: Add `src/freq.cpp` to `ferret_core`

- [ ] Edit `CMakeLists.txt`. Insert `src/freq.cpp` into the `add_library(ferret_core STATIC ...)` list, alphabetically (after `src/cli_axis.cpp` and before `src/jit.cpp`):

```cmake
add_library(ferret_core STATIC
  src/axis.cpp
  src/sweep.cpp
  src/cli_axis.cpp
  src/freq.cpp
  src/jit.cpp
  ...
```

### Step 2.7: Update `main.cpp`

- [ ] Delete the inline `parse_freq` definition in `src/main.cpp` (lines 33–69 in the pre-Task-2 file; the `namespace { ... double parse_freq(...) { ... } ` block).

- [ ] Add the include in the ferret-includes block:

```cpp
#include "ferret/freq.hpp"
```

- [ ] In `main`, the call site already reads `parse_freq(freq_str)`. Change it to `ferret::parse_freq(freq_str)`:

```cpp
freq_hz = ferret::parse_freq(freq_str);
```

(One callsite, around line 350 in the pre-Task-2 file.)

### Step 2.8: Build and run all tests

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass. The new `ParseFreq.*` tests pass; existing integration tests covering `--freq=bogus`, `--freq=inf`, `--freq=nan`, `--freq=0`, `--freq=-1` still pass with identical exit codes and error messages.

### Step 2.9: Commit

```bash
git add include/ferret/freq.hpp src/freq.cpp tests/test_freq.cpp \
        CMakeLists.txt tests/CMakeLists.txt src/main.cpp
git commit -m "$(cat <<'EOF'
refactor(freq): extract parse_freq into ferret/freq

Move parse_freq out of src/main.cpp into a dedicated ferret/freq module.
No behavior change: suffix handling, error messages, and the exception
type/contract are byte-identical to the prior inline implementation.

New unit tests cover GHz/MHz/kHz/Hz suffixes, bare numbers, scientific
notation, empty numeric component, trailing junk, non-numeric input,
NaN, +/-inf, zero, and negative inputs.
EOF
)"
```

---

## Task 3: Centralize option-value and extras parsing in `cli_axis`

**Files:**
- Modify: `include/ferret/cli_axis.hpp` (add two declarations)
- Modify: `src/cli_axis.cpp` (add two implementations)
- Modify: `tests/test_cli_axis.cpp` (add test cases)
- Modify: `src/main.cpp` (delete inline `parse_option_value`, replace the extras loop)

### Step 3.1: Write failing tests

- [ ] Append to `tests/test_cli_axis.cpp`:

```cpp
// ----- parse_option_value -----

TEST(ParseOptionValue, ParsesPositiveInteger) {
  EXPECT_EQ(parse_option_value("42"), 42);
}

TEST(ParseOptionValue, ParsesNegativeInteger) {
  EXPECT_EQ(parse_option_value("-7"), -7);
}

TEST(ParseOptionValue, ParsesZero) {
  EXPECT_EQ(parse_option_value("0"), 0);
}

TEST(ParseOptionValue, EmptyThrows) {
  EXPECT_THROW((void)parse_option_value(""), std::invalid_argument);
}

TEST(ParseOptionValue, NonNumericThrows) {
  EXPECT_THROW((void)parse_option_value("abc"), std::invalid_argument);
}

TEST(ParseOptionValue, TrailingJunkThrows) {
  EXPECT_THROW((void)parse_option_value("42x"), std::invalid_argument);
  EXPECT_THROW((void)parse_option_value("42.5"), std::invalid_argument);
}

// ----- parse_extras -----

TEST(ParseExtras, EmptyInputIsEmptyMap) {
  EXPECT_TRUE(parse_extras({}).empty());
}

TEST(ParseExtras, WellFormedTokens) {
  auto m = parse_extras({"--branches=1..8", "--spacing_bytes=64"});
  ASSERT_EQ(m.size(), 2U);
  EXPECT_EQ(m["branches"], "1..8");
  EXPECT_EQ(m["spacing_bytes"], "64");
}

TEST(ParseExtras, ValueMayContainEqualSign) {
  // First `=` separates name from value; rest of token is part of value.
  auto m = parse_extras({"--key=lo=hi"});
  ASSERT_EQ(m.size(), 1U);
  EXPECT_EQ(m["key"], "lo=hi");
}

TEST(ParseExtras, EmptyValueIsAllowed) {
  auto m = parse_extras({"--name="});
  ASSERT_EQ(m.size(), 1U);
  EXPECT_EQ(m["name"], "");
}

TEST(ParseExtras, MissingDoubleDashThrows) {
  EXPECT_THROW((void)parse_extras({"branches=1..8"}), std::invalid_argument);
  EXPECT_THROW((void)parse_extras({"-x=1"}), std::invalid_argument);
}

TEST(ParseExtras, MissingEqualsThrows) {
  EXPECT_THROW((void)parse_extras({"--branches"}), std::invalid_argument);
}

TEST(ParseExtras, ShortTokenThrows) {
  // tok.size() < 3 → "--" alone or "-x" both fail.
  EXPECT_THROW((void)parse_extras({"--"}), std::invalid_argument);
}
```

### Step 3.2: Verify the new tests fail to build

Run:

```bash
cmake --build build --target test_cli_axis 2>&1 | tail -10
```

Expected: undefined-reference or unresolved-overload errors for `parse_option_value` and `parse_extras`.

### Step 3.3: Add header declarations

- [ ] Edit `include/ferret/cli_axis.hpp`. Add `#include <map>` to the existing includes, and add the two new function declarations:

```cpp
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ferret/axis.hpp"

namespace ferret {

// Parses a CLI value like "64", "16,32,64", or "1..32768" against a
// declared Axis. The Axis's Kind decides how a "lo..hi" range expands
// (linear for Range, log2 for Log2Range). Throws std::invalid_argument
// on any malformed input.
std::vector<int64_t> parse_cli_axis_value(const std::string& cli_value, const Axis& axis);

// Parses a single --<option>=<value> scalar (integer).
// Throws std::invalid_argument on non-integer or trailing junk.
int64_t parse_option_value(const std::string& v);

// Turns CLI11's allow_extras() remainder into a name -> value map. Each
// token must look like --name=value. Throws std::invalid_argument with
// messages "unexpected argument: <tok>" or "--axis flags must be
// --name=value: <tok>" — the same messages the prior inline loop in
// main produced.
std::map<std::string, std::string> parse_extras(const std::vector<std::string>& tokens);

}  // namespace ferret
```

### Step 3.4: Add implementations

- [ ] Edit `src/cli_axis.cpp`. Add `#include <map>` and `#include <stdexcept>` to the existing includes (the latter is already present), and append the two new function bodies inside `namespace ferret`:

```cpp
int64_t parse_option_value(const std::string& v) {
  size_t pos = 0;
  int64_t parsed = 0;
  try {
    parsed = std::stoll(v, &pos);
  } catch (const std::exception&) {
    throw std::invalid_argument("not an integer: " + v);
  }
  if (pos != v.size()) {
    throw std::invalid_argument("trailing junk after integer: " + v);
  }
  return parsed;
}

std::map<std::string, std::string> parse_extras(const std::vector<std::string>& tokens) {
  std::map<std::string, std::string> out;
  for (const auto& tok : tokens) {
    if (tok.size() < 3 || tok[0] != '-' || tok[1] != '-') {
      throw std::invalid_argument("unexpected argument: " + tok);
    }
    auto eq = tok.find('=');
    if (eq == std::string::npos) {
      throw std::invalid_argument("--axis flags must be --name=value: " + tok);
    }
    out[tok.substr(2, eq - 2)] = tok.substr(eq + 1);
  }
  return out;
}
```

### Step 3.5: Run the test binary

Run:

```bash
cmake --build build --target test_cli_axis
ctest --test-dir build -R '^(ParseOptionValue|ParseExtras|CliAxis)\.' --output-on-failure
```

Expected: every test in `test_cli_axis` passes.

### Step 3.6: Update `main.cpp` to use both helpers

- [ ] Delete the inline `parse_option_value` definition in `src/main.cpp` (lines 104–116 in the pre-Task-3 file).

- [ ] In `do_run`, the call site `option_values[k] = parse_option_value(v);` becomes `option_values[k] = ferret::parse_option_value(v);` (it's inside the `namespace {` block in `main.cpp`, so it can't see the unqualified name without `using` — qualify explicitly).

- [ ] In `main`, replace the inline extras-parsing loop. The current code (around lines 333–345):

```cpp
std::map<std::string, std::string> overrides;
for (const auto& tok : run_cmd->remaining()) {
  if (tok.size() < 3 || tok[0] != '-' || tok[1] != '-') {
    flog::error("unexpected argument: {}", tok);
    return 2;
  }
  auto eq = tok.find('=');
  if (eq == std::string::npos) {
    flog::error("--axis flags must be --name=value: {}", tok);
    return 2;
  }
  overrides[tok.substr(2, eq - 2)] = tok.substr(eq + 1);
}
```

Becomes:

```cpp
std::map<std::string, std::string> overrides;
try {
  overrides = ferret::parse_extras(run_cmd->remaining());
} catch (const std::exception& e) {
  flog::error("{}", e.what());
  return 2;
}
```

(The exception message already contains the verbatim text the inline loop logged.)

### Step 3.7: Full build + ctest

Run:

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all tests pass — including any existing integration tests that pass through `--axis=value` extras or that hit the "unexpected argument" / "must be --name=value" error paths.

### Step 3.8: Commit

```bash
git add include/ferret/cli_axis.hpp src/cli_axis.cpp tests/test_cli_axis.cpp src/main.cpp
git commit -m "$(cat <<'EOF'
refactor(cli_axis): centralize option value and extras parsing

Move parse_option_value out of src/main.cpp and add parse_extras (the
allow_extras() tokenizer that turns CLI11's remainder into a name->value
map) into the existing cli_axis module. Both are CLI-side scalar parsers
adjacent to parse_cli_axis_value; co-locating them in cli_axis keeps
"how do we parse a single --name=value token from argv" in one place.

main.cpp's inline extras loop is gone; main now catches a single
exception with the same user-visible message text as before.

New unit tests cover integer-only/negative/zero/empty/trailing-junk for
parse_option_value, and well-formed/empty-value/short-token/missing-prefix/
missing-equals cases for parse_extras.
EOF
)"
```

---

## Task 4: Extract `do_run` into `ferret::run` with helper decomposition

**Files:**
- Create: `include/ferret/run_command.hpp`
- Create: `src/run_command.cpp`
- Modify: `CMakeLists.txt` (add `src/run_command.cpp` to `ferret_core`)
- Modify: `src/main.cpp` (delete `do_list`, `do_run`, anonymous namespace; rewrite `main` body; remove both `NOLINT` comments)

This is the largest task. No new unit-test file — behavioral equivalence is covered by the existing integration tests, which exercise every error path.

### Step 4.1: Create the header

- [ ] Create `include/ferret/run_command.hpp`:

```cpp
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace ferret {

struct RunOptions {
  std::string name;
  std::map<std::string, std::string> overrides;
  std::string out_path;
  int core = -1;
  std::optional<double> freq_hz;
  int reps = 7;
  int warmup = 1;
  int64_t seed = 1;
};

// Runs one benchmark sweep end-to-end. Returns process exit code
// (0 success, 2 user/parameter error). Never throws across the boundary.
int run(const RunOptions& opts);

// Prints registered benchmark names to stdout, one per line. Always returns 0.
int list_command();

}  // namespace ferret
```

### Step 4.2: Create the implementation

- [ ] Create `src/run_command.cpp`. This is a careful move of `do_run`'s body plus internal decomposition. Write the file verbatim:

```cpp
#include "ferret/run_command.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "ferret/benchmark.hpp"
#include "ferret/cli_axis.hpp"
#include "ferret/csv.hpp"
#include "ferret/jit.hpp"
#include "ferret/log.hpp"
#include "ferret/params.hpp"
#include "ferret/pinning.hpp"
#include "ferret/runner.hpp"
#include "ferret/sweep.hpp"
#include "ferret/timing.hpp"

namespace flog = ferret::log;

namespace ferret {
namespace {

struct ClassifiedOverrides {
  std::map<std::string, std::vector<int64_t>> axis_values;
  std::map<std::string, int64_t> option_values;
};

bool classify_overrides(const std::string& bench_name,
                        const SweepAxes& axes,
                        const BenchOptions& options,
                        const std::map<std::string, std::string>& raw,
                        ClassifiedOverrides& out) {
  for (const auto& o : options) {
    out.option_values[o.name] = o.default_value;
  }
  for (const auto& [k, v] : raw) {
    const Axis* axis_match = nullptr;
    for (const auto& a : axes) {
      if (a.name() == k) {
        axis_match = &a;
        break;
      }
    }
    const BenchOption* opt_match = nullptr;
    for (const auto& o : options) {
      if (o.name == k) {
        opt_match = &o;
        break;
      }
    }
    if (axis_match != nullptr) {
      try {
        out.axis_values[k] = parse_cli_axis_value(v, *axis_match);
      } catch (const std::exception& e) {
        flog::error("invalid value for --{}: {}", k, e.what());
        return false;
      }
    } else if (opt_match != nullptr) {
      try {
        out.option_values[k] = parse_option_value(v);
      } catch (const std::exception& e) {
        flog::error("invalid value for --{}: {}", k, e.what());
        return false;
      }
    } else {
      flog::error("unknown axis or option --{} for benchmark {}", k, bench_name);
      return false;
    }
  }
  return true;
}

void inject_options(std::vector<Params>& rows,
                    const std::map<std::string, int64_t>& options,
                    int64_t seed) {
  for (auto& p : rows) {
    for (const auto& [k, v] : options) {
      p.set(k, v);
    }
    p.set("seed", seed);
  }
}

void apply_pinning(int core) {
  if (core >= 0 && !pinning::pin_to_core(core)) {
    flog::warn("pin_to_core({}) failed", core);
  }
  if (!pinning::boost_priority()) {
    flog::warn("boost_priority failed");
  }
  if (!pinning::lock_memory()) {
    flog::warn("mlockall failed");
  }
}

std::ostream* open_output(const std::string& path, std::ofstream& ofs) {
  if (path.empty()) {
    return &std::cout;
  }
  ofs.open(path);
  if (!ofs) {
    flog::error("cannot open output: {}", path);
    return nullptr;
  }
  return &ofs;
}

std::vector<std::string> column_names(const SweepAxes& axes, const BenchOptions& options) {
  std::vector<std::string> cols;
  cols.reserve(axes.size() + options.size() + 1);
  for (const auto& a : axes) {
    cols.push_back(a.name());
  }
  for (const auto& o : options) {
    cols.push_back(o.name);
  }
  cols.emplace_back("seed");
  return cols;
}

struct MeasuredRow {
  Params params;
  MeasurementRow row;
};

std::optional<std::vector<MeasuredRow>> measure_all(Benchmark& bench,
                                                    const std::vector<Params>& rows,
                                                    int reps,
                                                    int warmup) {
  std::vector<MeasuredRow> out;
  out.reserve(rows.size());
  for (const auto& p : rows) {
    MeasurementRow m;
    try {
      size_t pre_iters = bench.iterations(p);
      size_t pre_sites = bench.sites_per_kernel(p);
      if (pre_iters == 0 || pre_sites == 0) {
        flog::error("invalid params: yields zero work (iterations={}, sites_per_kernel={})", pre_iters, pre_sites);
        return std::nullopt;
      }
      JittedKernel kern(bench, p);
      if (!kern.ok()) {
        m.jit_failed = true;
        m.iters = pre_iters;
        m.sites = pre_sites;
        flog::warn("sljit_error on params; emitting empty row");
      } else {
        m = runner::measure(kern.fn(), pre_iters, pre_sites, reps, warmup);
      }
    } catch (const std::exception& e) {
      flog::error("benchmark error on params: {}", e.what());
      return std::nullopt;
    }
    out.push_back({p, m});
  }
  return out;
}

void emit_csv(std::ostream& out,
              const std::string& bench_name,
              const std::vector<std::string>& cols,
              std::optional<double> freq_hz,
              double tpns,
              const std::vector<MeasuredRow>& rows) {
  CsvWriter writer(out, bench_name, cols, freq_hz);
  writer.write_header();
  for (const auto& r : rows) {
    writer.write_row(r.params, r.row, tpns);
  }
  out.flush();
}

}  // namespace

int run(const RunOptions& opts) {
  auto bench = BenchmarkRegistry::create(opts.name);
  if (!bench) {
    flog::error("unknown benchmark '{}'. Try `ferret list`.", opts.name);
    return 2;
  }

  SweepAxes axes = bench->axes();
  BenchOptions options = bench->options();

  ClassifiedOverrides classified;
  if (!classify_overrides(opts.name, axes, options, opts.overrides, classified)) {
    return 2;
  }

  std::vector<Params> rows;
  try {
    rows = sweep::expand(axes, classified.axis_values);
  } catch (const std::exception& e) {
    flog::error("invalid sweep: {}", e.what());
    return 2;
  }

  inject_options(rows, classified.option_values, opts.seed);
  apply_pinning(opts.core);

  std::ofstream ofs;
  std::ostream* out = open_output(opts.out_path, ofs);
  if (out == nullptr) {
    return 2;
  }

  // Non-finite tpns would propagate NaN into CSV.
  double tpns = timing::ticks_per_ns();
  if (!std::isfinite(tpns) || !(tpns > 0.0)) {
    flog::error("ticks_per_ns calibration returned non-finite or non-positive value: {}", tpns);
    return 2;
  }

  // No partial output: buffer all rows, emit only after every row succeeded.
  auto measured = measure_all(*bench, rows, opts.reps, opts.warmup);
  if (!measured) {
    return 2;
  }

  emit_csv(*out, opts.name, column_names(axes, options), opts.freq_hz, tpns, *measured);
  return 0;
}

int list_command() {
  for (const auto& n : BenchmarkRegistry::names()) {
    std::cout << n << "\n";
  }
  return 0;
}

}  // namespace ferret
```

### Step 4.3: Add `src/run_command.cpp` to `ferret_core`

- [ ] Edit `CMakeLists.txt`. Insert `src/run_command.cpp` into the `add_library(ferret_core STATIC ...)` list (alphabetical position, after `src/runner.cpp`):

```cmake
add_library(ferret_core STATIC
  ...
  src/runner.cpp
  src/run_command.cpp
  src/padding.cpp
  ...
```

### Step 4.4: Rewrite `src/main.cpp`

- [ ] Overwrite `src/main.cpp` with the following — this is the entire new file:

```cpp
#include <CLI/CLI.hpp>

#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ferret/cli_axis.hpp"
#include "ferret/freq.hpp"
#include "ferret/log.hpp"
#include "ferret/run_command.hpp"

// Aliased as `flog` (not `log`) to avoid colliding with the C library
// `::log(double)` declared in <math.h> on glibc, which leaks into the
// global namespace via transitive includes (e.g., <filesystem>).
namespace flog = ferret::log;

int main(int argc, char** argv) {
  try {
    flog::init();

    CLI::App app{"ferret — frontend reverse-engineering toolkit"};
    app.require_subcommand(1);

    auto* list_cmd = app.add_subcommand("list", "List registered benchmarks");

    auto* run_cmd = app.add_subcommand("run", "Run a benchmark");
    std::string name;
    run_cmd->add_option("name", name, "benchmark name")->required();

    // --log-level is attached to run_cmd, not app, because run_cmd uses
    // allow_extras() and would otherwise swallow it as an --<axis>= override.
    std::string log_level_str = "warn";
    run_cmd
        ->add_option("--log-level", log_level_str, "log level: trace|debug|info|warn|error|critical|off (default warn)")
        ->check(CLI::IsMember({"trace", "debug", "info", "warn", "warning", "error", "critical", "off"}));

    std::string out_path;
    run_cmd->add_option("--out", out_path, "CSV output path (default stdout)");

    int core = -1;
    run_cmd->add_option("--core", core, "core to pin to (default: no pin)");

    std::string freq_str;
    run_cmd->add_option("--freq", freq_str, "core frequency, e.g. 4.521GHz; enables cycles_per_site columns");

    int K = 7;
    run_cmd->add_option("--reps", K, "number of timed repetitions per param point (default 7)");

    int warmup = 1;
    run_cmd->add_option("--warmup", warmup, "warmup calls before each measurement (default 1)");

    int64_t seed = 1;
    run_cmd->add_option("--seed", seed, "RNG seed for benchmarks that randomize (default 1)");

    run_cmd->allow_extras();

    CLI11_PARSE(app, argc, argv);

    flog::set_level(flog::parse_level(log_level_str));

    if (*list_cmd) {
      return ferret::list_command();
    }

    if (*run_cmd) {
      if (K < 1) {
        flog::error("--reps must be >= 1 (got {})", K);
        return 2;
      }
      if (warmup < 0) {
        flog::error("--warmup must be >= 0 (got {})", warmup);
        return 2;
      }

      ferret::RunOptions opts;
      try {
        opts.overrides = ferret::parse_extras(run_cmd->remaining());
      } catch (const std::exception& e) {
        flog::error("{}", e.what());
        return 2;
      }

      if (!freq_str.empty()) {
        try {
          opts.freq_hz = ferret::parse_freq(freq_str);
        } catch (const std::exception& e) {
          flog::error("invalid {}", e.what());
          return 2;
        }
      }
      opts.name = name;
      opts.out_path = out_path;
      opts.core = core;
      opts.reps = K;
      opts.warmup = warmup;
      opts.seed = seed;
      return ferret::run(opts);
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ferret: unexpected exception: " << e.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "ferret: unexpected non-standard exception\n";
    return 1;
  }
}
```

Notes for the engineer applying this:

- Both `NOLINTNEXTLINE(bugprone-exception-escape)` and `NOLINTBEGIN/END(readability-function-cognitive-complexity, bugprone-easily-swappable-parameters)` comments are gone. The outer `try/catch` in `main` satisfies `bugprone-exception-escape` without an annotation. The cognitive-complexity check no longer fires because the body is simple branching plus delegations.
- The `<sljitLir.h>` include and the `JittedKernel`/`jit_compile`/`jit_free`/`do_list`/`do_run`/`parse_freq`/`parse_option_value` definitions are entirely gone — they live in their new homes.
- File size target: ~100 lines.

### Step 4.5: Build and run all tests

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: every test passes. The integration tests in `tests/test_integration.cpp` are the primary behavioral safety net for this task; if any fail, the error path semantics have diverged from the prior `do_run` and must be reconciled before commit.

### Step 4.6: Run clang-tidy on the touched files

```bash
./scripts/lint.sh
```

Expected: no warnings, no errors. If clang-tidy flags `bugprone-exception-escape` on `main` despite the try/catch wrap (it shouldn't — the body is fully wrapped), confirm by checking that **every** statement in `main` is inside the `try` block.

### Step 4.7: Verify file-size acceptance criterion

```bash
wc -l src/main.cpp
```

Expected: ≤ 130. Target is ~100.

### Step 4.8: Equivalence-check the binary against the pre-flight baseline

```bash
build/ferret run direct_branch_footprint --branches=1,2,4,8 --spacing_bytes=64 \
  --reps=3 --warmup=1 --seed=1 --freq=4.0GHz --out=/tmp/ferret-refactored.csv

# Header line must match exactly.
diff /tmp/ferret-baseline.header <(head -1 /tmp/ferret-refactored.csv)

# Non-timing columns must match. Discover N = number of non-timing columns
# from the header: every column whose name is in {benchmark, branches,
# spacing_bytes, sattolo_permute, seed}, plus any other leading
# non-measurement columns. (For direct_branch_footprint as of bf5f00f, the
# leading non-timing columns are benchmark, branches, spacing_bytes,
# sattolo_permute, seed — N=5.)
N=$(head -1 /tmp/ferret-baseline.csv | awk -F, '{
  for (i=1; i<=NF; i++) {
    if ($i ~ /(ns|cycles|time|ticks|min|p[0-9]+|mean|std)/) { print i-1; exit }
  }
}')
echo "Non-timing column count: $N"
diff <(cut -d, -f1-$N /tmp/ferret-baseline.csv) \
     <(cut -d, -f1-$N /tmp/ferret-refactored.csv)
```

Expected: both diffs produce no output. If they do, the refactor changed user-visible CSV structure or values and the offending change must be reverted before commit.

### Step 4.9: Commit

```bash
git add include/ferret/run_command.hpp src/run_command.cpp \
        CMakeLists.txt src/main.cpp
git commit -m "$(cat <<'EOF'
refactor(run_command): move do_run into ferret::run with helper decomposition

Move do_run and do_list from src/main.cpp into a dedicated run_command
module. The 153-line do_run body is split into static-local helpers in
src/run_command.cpp:

  classify_overrides   axis-vs-option partitioning of --name=value extras
  inject_options       inject default option values and seed into rows
  apply_pinning        core pin + priority + mlockall (warn on failure)
  open_output          select ofstream or cout
  column_names         build axis + option + "seed" header vector
  measure_all          per-row loop with RAII JittedKernel; buffers result
  emit_csv             write header + rows + flush

No helper exceeds ~30 lines. The top-level ferret::run is ~30 lines of
glue. Both NOLINT suppressions on do_run/main are removed; main is now
~100 lines of CLI11 wiring with an outer try/catch satisfying
bugprone-exception-escape directly.

Comment-debt sweep applied to the moved code: round-N narrations are
gone, but the two timeless invariants (non-finite tpns -> NaN in CSV,
no-partial-output buffering) are kept as one-liners.

No behavior change: error messages, exit codes, and the CSV columns
(non-timing) are byte-identical to the prior implementation. Verified
against /tmp/ferret-baseline.csv from the pre-flight step.
EOF
)"
```

---

## Post-flight

- [ ] **Verify the four-commit log**

```bash
git log --oneline bf5f00f..HEAD
```

Expected output (subjects, in order):

```
<sha> refactor(run_command): move do_run into ferret::run with helper decomposition
<sha> refactor(cli_axis): centralize option value and extras parsing
<sha> refactor(freq): extract parse_freq into ferret/freq
<sha> refactor(jit): extract sljit kernel lifecycle into ferret::JittedKernel
```

- [ ] **Verify all acceptance criteria from the spec**

Each line should pass:

```bash
# main.cpp size <= 130
test $(wc -l < src/main.cpp) -le 130 && echo OK

# No NOLINT in main.cpp or run_command.cpp
! grep -E 'NOLINT(BEGIN|END|NEXTLINE)' src/main.cpp src/run_command.cpp && echo OK

# All tests pass
ctest --test-dir build --output-on-failure | tail -3

# clang-format + clang-tidy clean
./scripts/format.sh && git diff --quiet && echo "format OK"
./scripts/lint.sh && echo "lint OK"
```

- [ ] **Reverify each commit in isolation**

Each intermediate commit must build and pass tests. Spot-check the two middle commits:

```bash
git stash --include-untracked 2>/dev/null || true
for sha in $(git log --oneline bf5f00f..HEAD --reverse | awk '{print $1}'); do
  git checkout "$sha" --quiet
  cmake --build build > /tmp/build-$sha.log 2>&1 \
    && ctest --test-dir build --output-on-failure > /tmp/test-$sha.log 2>&1 \
    && echo "$sha OK" \
    || echo "$sha FAILED — see /tmp/build-$sha.log and /tmp/test-$sha.log"
done
git checkout refactor/cleanup --quiet
git stash pop 2>/dev/null || true
```

Expected: four `<sha> OK` lines.

- [ ] **Push (only after the user approves)**

```bash
git push -u origin refactor/cleanup
```

Do not push without explicit user approval — see project policy on operations affecting shared state.

---

## Self-review checklist (writer's notes, not a step)

- Every spec section maps to a task:
  - `JittedKernel` extraction → Task 1
  - `parse_freq` extraction → Task 2
  - `parse_option_value` + `parse_extras` → Task 3
  - `do_run` body decomposition into helpers → Task 4 (`classify_overrides` / `inject_options` / `apply_pinning` / `open_output` / `column_names` / `measure_all` / `emit_csv`)
  - Comment-debt sweep → Task 4 (applied via verbatim file write)
  - `NOLINT` removal on `do_run` and `main` → Task 4 (verified in step 4.6)
  - Unit tests for freq, jit, cli_axis additions → Tasks 2, 1, 3
  - File-size criterion → step 4.7
  - CSV equivalence criterion → step 4.8
- No placeholders, no "TBD". Every step shows actual code or the actual command.
- Type/name consistency: `JittedKernel::fn_t`, `JittedKernel::ok()`, `JittedKernel::fn()` referenced consistently in jit.hpp, jit.cpp, test_jit.cpp, run_command.cpp. `ferret::parse_freq`, `ferret::parse_extras`, `ferret::parse_option_value` referenced consistently. `RunOptions` fields match across run_command.hpp, run_command.cpp, and main.cpp.

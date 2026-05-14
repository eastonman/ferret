# `geom_range` Axis Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a new `Axis::GeomRange` kind that produces a geometric sweep with `k` samples per octave, expose it via a new CLI `lo..hi@k` range suffix, and migrate `direct_branch_footprint`'s `branches` axis to `geom_range(..., 1)` so users can zoom into BTB capacity cliffs without hand-curated value lists.

**Architecture:** Four self-contained commits. (1) New `Axis::GeomRange` kind + `expand_geom_range` free function; with `k=1` it is byte-identical to `expand_log2_range`, which the first unit test pins. (2) CLI parser learns the `@k` suffix on `lo..hi` tokens; rejected on non-`geom_range` axes. (3) `direct_branch_footprint` switches its `branches` axis declaration to `geom_range(..., samples_per_octave=1)` and gains an end-to-end integration test using `@4`. (4) Docs updates (architecture, writing-a-benchmark, README CLI section).

**Tech Stack:** C++20, CMake/Ninja, GoogleTest, sljit, CLI11, spdlog, nix-managed toolchain.

**Spec:** `docs/superpowers/specs/2026-05-14-geom-range-axis-design.md`

**Baseline:** `79cef7d` (origin/main HEAD at plan time). Working branch: `worktree-feat+geom-range-axis` in worktree `/Users/easton/WorkingSpace/project/ferret/main/.claude/worktrees/feat+geom-range-axis`.

---

## Pre-flight

All commands run from the worktree root. The toolchain lives in the
project's nix flake; either enter `nix develop` once and run the commands
inside that shell, or prefix every command with
`nix develop --command bash -lc '<cmd>'`. The rest of this plan shows the
bare commands; adapt as needed.

- [ ] **Verify clean baseline**

```bash
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected on aarch64-darwin: `112/112 tests passed`, with
`Padding.EmitsRequestedBytesOnX86_64` reported as skipped. If anything
else is skipped or any test fails, stop and investigate — every later
step assumes a clean baseline.

- [ ] **Capture an equivalence-check baseline CSV**

```bash
build/ferret run direct_branch_footprint \
  --branches=1..32768 --spacing_bytes=64 \
  --reps=3 --warmup=1 --seed=1 \
  --out=/tmp/geom-range-pre.csv
```

Save this file. After Task 3 (which migrates the axis to `geom_range(... ,1)`),
re-run the same command into `/tmp/geom-range-post.csv` and diff them.
The `branches` column must be identical row-for-row.

---

## Task 1: Add `Axis::GeomRange` kind + `expand_geom_range`

**Files:**
- Modify: `include/ferret/axis.hpp`
- Modify: `src/axis.cpp`
- Modify: `tests/test_params_axis.cpp`

This task adds a new axis kind to the public API and the free function
that implements its expansion. `k=1` parity with `expand_log2_range` is
pinned by the first new test, so this task is also a regression guard
for Task 3 (the `direct_branch_footprint` migration).

- [ ] **Step 1: Add failing unit tests**

Append to `tests/test_params_axis.cpp` (after the existing
`Axis.Log2RangeWithNegativeLoThrows` test):

```cpp
TEST(Axis, GeomRangeWithKOneEqualsLog2Range) {
  Axis g = Axis::geom_range("branches", 1, 32768, 1);
  Axis l = Axis::log2_range("branches", 1, 32768);
  EXPECT_EQ(g.expand(), l.expand());
}

TEST(Axis, GeomRangeFourSamplesAcrossThreeOctaves) {
  // round(1 * 2^(i/4)) for i = 0..12, dedup adjacent duplicates, with
  // hi=8 already on the natural sequence so no hi-forcing fires.
  Axis a = Axis::geom_range("branches", 1, 8, 4);
  EXPECT_EQ(a.expand(), (std::vector<int64_t>{1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST(Axis, GeomRangeFourSamplesInOneOctave) {
  Axis a = Axis::geom_range("branches", 1024, 2048, 4);
  EXPECT_EQ(a.expand(),
            (std::vector<int64_t>{1024, 1218, 1448, 1722, 2048}));
}

TEST(Axis, GeomRangeForcesHiAsFinalPoint) {
  // {1, 2, 4, 8} is the natural k=1 expansion up to 10; hi=10 is
  // appended because the last natural point (8) is strictly less.
  Axis a = Axis::geom_range("branches", 1, 10, 1);
  EXPECT_EQ(a.expand(), (std::vector<int64_t>{1, 2, 4, 8, 10}));
}

TEST(Axis, GeomRangeSinglePointWhenLoEqualsHi) {
  Axis a = Axis::geom_range("branches", 5, 5, 4);
  EXPECT_EQ(a.expand(), (std::vector<int64_t>{5}));
}

TEST(Axis, GeomRangeWithZeroLoThrows) {
  Axis a = Axis::geom_range("branches", 0, 8, 1);
  EXPECT_THROW((void)a.expand(), std::invalid_argument);
}

TEST(Axis, GeomRangeWithNegativeLoThrows) {
  Axis a = Axis::geom_range("branches", -1, 8, 1);
  EXPECT_THROW((void)a.expand(), std::invalid_argument);
}

TEST(Axis, GeomRangeWithHiBelowLoThrows) {
  Axis a = Axis::geom_range("branches", 8, 4, 1);
  EXPECT_THROW((void)a.expand(), std::invalid_argument);
}

TEST(Axis, GeomRangeWithNonPositiveKThrows) {
  Axis a0 = Axis::geom_range("branches", 1, 8, 0);
  EXPECT_THROW((void)a0.expand(), std::invalid_argument);
  Axis aneg = Axis::geom_range("branches", 1, 8, -2);
  EXPECT_THROW((void)aneg.expand(), std::invalid_argument);
}

TEST(Axis, GeomRangeSamplesPerOctaveAccessor) {
  EXPECT_EQ(Axis::geom_range("x", 1, 8, 4).samples_per_octave(), 4);
  // Non-geom_range axes report 1 so callers have a sane default.
  EXPECT_EQ(Axis::log2_range("x", 1, 8).samples_per_octave(), 1);
  EXPECT_EQ(Axis::range("x", 0, 10).samples_per_octave(), 1);
  EXPECT_EQ(Axis::values("x", {1, 2, 3}).samples_per_octave(), 1);
}
```

- [ ] **Step 2: Run tests; verify they fail**

```bash
cmake --build build --target test_params_axis 2>&1 | tail -20
```

Expected: compile error such as `'geom_range' is not a member of 'ferret::Axis'`
and/or `'samples_per_octave' was not declared`. The tests do not compile
because the API does not exist yet — this is the desired failure for
this step.

- [ ] **Step 3: Extend the public header**

Replace `include/ferret/axis.hpp` in full with:

```cpp
#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace ferret {

// Declarative parameter axis used by a Benchmark's axes() method. Four
// kinds: Range (inclusive linear), Log2Range (powers of two up to hi),
// GeomRange (k samples per octave, generalization of Log2Range),
// Values (literal list). expand() materializes the chosen kind into a
// concrete int64_t value list.
class Axis {
 public:
  enum class Kind { Range, Log2Range, GeomRange, Values };

  // Closed interval [lo, hi]. Step is 1.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  static Axis range(std::string name, int64_t lo, int64_t hi);

  // {lo, lo*2, lo*4, ...} up to the largest power of two <= hi.
  // Delegates to expand_log2_range; throws std::invalid_argument when
  // lo <= 0.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  static Axis log2_range(std::string name, int64_t lo, int64_t hi);

  // Geometric sweep with `samples_per_octave` points per doubling
  // between `lo` and `hi` inclusive. Equivalent to log2_range when
  // samples_per_octave == 1. Delegates to expand_geom_range; throws
  // std::invalid_argument when lo <= 0, hi < lo, or
  // samples_per_octave <= 0.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  static Axis geom_range(std::string name, int64_t lo, int64_t hi, int64_t samples_per_octave);

  // Uses the supplied list verbatim; no validation.
  static Axis values(std::string name, std::vector<int64_t> vs);

  const std::string& name() const { return name_; }
  Kind kind() const { return kind_; }

  // GeomRange axes return their declared k; all other kinds return 1.
  // Used by the CLI parser to default the `@k` suffix when omitted.
  int64_t samples_per_octave() const { return k_; }

  // May throw on Log2Range/GeomRange via expand_log2_range /
  // expand_geom_range.
  std::vector<int64_t> expand() const;

 private:
  Axis(std::string name, Kind kind) : name_(std::move(name)), kind_(kind) {}

  std::string name_;
  Kind kind_;
  int64_t lo_ = 0;
  int64_t hi_ = 0;
  int64_t k_ = 1;
  std::vector<int64_t> values_;
};

using SweepAxes = std::vector<Axis>;

// Expands a log2 range [lo, hi] into {lo, lo*2, lo*4, ...} up to the
// largest power-of-two not exceeding hi. Throws std::invalid_argument
// when lo <= 0. Stops when the next doubling would overflow int64_t.
// `context` is prepended to the error message (e.g., axis name or CLI
// fragment) so the user sees what value triggered the failure.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<int64_t> expand_log2_range(int64_t lo, int64_t hi, std::string_view context = {});

// Expands a geometric range [lo, hi] sampling `k` points per octave:
// {round(lo * 2^(i/k))} for i = 0, 1, ... while the value <= hi.
// Adjacent duplicate rounded values are deduped. `hi` is appended as
// the final point when the natural sequence does not land on it. k=1
// reproduces expand_log2_range exactly. Throws std::invalid_argument
// when lo <= 0, hi < lo, or k <= 0. `context` is prepended to the
// error message.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<int64_t> expand_geom_range(int64_t lo, int64_t hi, int64_t k, std::string_view context = {});

}  // namespace ferret
```

- [ ] **Step 4: Extend the source file**

Replace `src/axis.cpp` in full with:

```cpp
#include "ferret/axis.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ferret {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Axis Axis::range(std::string name, int64_t lo, int64_t hi) {
  Axis a(std::move(name), Kind::Range);
  a.lo_ = lo;
  a.hi_ = hi;
  return a;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Axis Axis::log2_range(std::string name, int64_t lo, int64_t hi) {
  Axis a(std::move(name), Kind::Log2Range);
  a.lo_ = lo;
  a.hi_ = hi;
  return a;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Axis Axis::geom_range(std::string name, int64_t lo, int64_t hi, int64_t samples_per_octave) {
  Axis a(std::move(name), Kind::GeomRange);
  a.lo_ = lo;
  a.hi_ = hi;
  a.k_ = samples_per_octave;
  return a;
}

Axis Axis::values(std::string name, std::vector<int64_t> vs) {
  Axis a(std::move(name), Kind::Values);
  a.values_ = std::move(vs);
  return a;
}

std::vector<int64_t> Axis::expand() const {
  std::vector<int64_t> out;
  switch (kind_) {
    case Kind::Range:
      for (int64_t v = lo_; v <= hi_; ++v) {
        out.push_back(v);
      }
      return out;
    case Kind::Log2Range:
      return expand_log2_range(lo_, hi_, "Axis '" + name_ + "'");
    case Kind::GeomRange:
      return expand_geom_range(lo_, hi_, k_, "Axis '" + name_ + "'");
    case Kind::Values:
      return values_;
  }
  return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<int64_t> expand_log2_range(int64_t lo, int64_t hi, std::string_view context) {
  if (lo <= 0) {
    std::string msg;
    if (!context.empty()) {
      msg.append(context).append(": ");
    }
    msg.append("log2 range requires lo > 0");
    throw std::invalid_argument(msg);
  }
  std::vector<int64_t> out;
  // Pre-multiply overflow guard (signed overflow is UB).
  constexpr int64_t kHalfMax = std::numeric_limits<int64_t>::max() / 2;
  for (int64_t v = lo; v <= hi;) {
    out.push_back(v);
    if (v > kHalfMax) {
      break;
    }
    v *= 2;
  }
  return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<int64_t> expand_geom_range(int64_t lo, int64_t hi, int64_t k, std::string_view context) {
  auto throw_with_ctx = [&](const char* what) {
    std::string msg;
    if (!context.empty()) {
      msg.append(context).append(": ");
    }
    msg.append(what);
    throw std::invalid_argument(msg);
  };
  if (lo <= 0) {
    throw_with_ctx("geom range requires lo > 0");
  }
  if (hi < lo) {
    throw_with_ctx("geom range requires hi >= lo");
  }
  if (k <= 0) {
    throw_with_ctx("geom range requires samples_per_octave >= 1");
  }

  std::vector<int64_t> out;
  const auto lo_d = static_cast<double>(lo);
  const auto k_d = static_cast<double>(k);
  // Loop bound: ceil(k * log2(hi/lo)) + 1 is the maximum number of
  // generated points; the +2 leaves slack for floating rounding so the
  // loop exits via the v > hi check below, not the upper bound.
  const auto max_i = static_cast<int64_t>(k_d * std::log2(static_cast<double>(hi) / lo_d)) + 2;
  for (int64_t i = 0; i <= max_i; ++i) {
    double exact = lo_d * std::exp2(static_cast<double>(i) / k_d);
    // Defensive overflow check; reachable only if max_i was too generous.
    if (exact > static_cast<double>(std::numeric_limits<int64_t>::max())) {
      break;
    }
    int64_t v = static_cast<int64_t>(std::llround(exact));
    if (v > hi) {
      break;
    }
    if (!out.empty() && v == out.back()) {
      continue;
    }
    out.push_back(v);
  }
  if (out.empty() || out.back() < hi) {
    out.push_back(hi);
  }
  return out;
}

}  // namespace ferret
```

- [ ] **Step 5: Build and run the new tests; verify they pass**

```bash
cmake --build build --target test_params_axis
ctest --test-dir build -R '^Axis\.' --output-on-failure
```

Expected: all `Axis.*` tests pass, including the nine new
`Axis.GeomRange*` tests. The `Axis.GeomRangeWithKOneEqualsLog2Range`
test is the parity pin — if it fails, the `k=1` invariant is broken
and Task 3 will silently regress `direct_branch_footprint`.

- [ ] **Step 6: Run the full test suite; verify nothing else broke**

```bash
ctest --test-dir build --output-on-failure
```

Expected: `112/112 tests passed` (one platform-skip on
`Padding.EmitsRequestedBytesOnX86_64` when running on aarch64).

- [ ] **Step 7: Format + lint pass**

```bash
./scripts/format.sh
./scripts/lint.sh
```

Expected: both exit `0`. If `clang-tidy` complains about
`bugprone-easily-swappable-parameters` on `expand_geom_range` or
`Axis::geom_range`, the `// NOLINTNEXTLINE` comment in the code above
should already suppress it; verify the comment is immediately above
the declaration (no blank line).

- [ ] **Step 8: Commit**

```bash
git add include/ferret/axis.hpp src/axis.cpp tests/test_params_axis.cpp
git commit -m "feat(axis): add GeomRange kind with k samples per octave"
```

---

## Task 2: CLI `lo..hi@k` suffix parsing

**Files:**
- Modify: `src/cli_axis.cpp`
- Modify: `tests/test_cli_axis.cpp`

This task teaches `parse_cli_axis_value` one new token shape: `lo..hi@k`.
The suffix is valid only on `GeomRange` axes; it is rejected on
`Log2Range`, `Range`, and `Values` axes to keep sweep semantics tied to
the declaration rather than the CLI string.

- [ ] **Step 1: Add failing CLI tests**

Append to `tests/test_cli_axis.cpp` (after the existing
`CliAxis.RangeAxisAcceptsZeroAndNegativeLists` test):

```cpp
TEST(CliAxis, GeomRangeWithoutSuffixUsesAxisK) {
  // Picked so the result obviously differs from the linear-range
  // fallback (which would be 101 elements for "100..200"); guards
  // against a regression where parse_cli_axis_value forgets the
  // GeomRange case and falls through.
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 4);
  auto v = parse_cli_axis_value("100..200", branches);
  EXPECT_EQ(v, (std::vector<int64_t>{100, 119, 141, 168, 200}));
}

TEST(CliAxis, GeomRangeAtSuffixOverridesAxisK) {
  // Axis declares k=4 but CLI passes @1 — CLI wins.
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 4);
  auto v = parse_cli_axis_value("1..8@1", branches);
  EXPECT_EQ(v, (std::vector<int64_t>{1, 2, 4, 8}));
}

TEST(CliAxis, GeomRangeAtSuffixDensifies) {
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 1);
  auto v = parse_cli_axis_value("1024..2048@4", branches);
  EXPECT_EQ(v, (std::vector<int64_t>{1024, 1218, 1448, 1722, 2048}));
}

TEST(CliAxis, GeomRangeAtSuffixOnLog2AxisThrowsWithSpecificMessage) {
  // Without @k handling, parse_int("32768@4") happens to throw too —
  // but with the wrong message. Asserting on the message text pins
  // that the rejection comes from the @k validator, not a coincidence.
  Axis branches = Axis::log2_range("branches", 1, 1 << 15);
  try {
    (void)parse_cli_axis_value("1..32768@4", branches);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    std::string what(e.what());
    EXPECT_NE(what.find("only valid for geom_range"), std::string::npos) << "got: " << what;
  }
}

TEST(CliAxis, GeomRangeAtSuffixOnLinearRangeAxisThrowsWithSpecificMessage) {
  Axis x = Axis::range("x", 0, 100);
  try {
    (void)parse_cli_axis_value("1..10@2", x);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    std::string what(e.what());
    EXPECT_NE(what.find("only valid for geom_range"), std::string::npos) << "got: " << what;
  }
}

TEST(CliAxis, GeomRangeAtSuffixRejectsNonPositiveK) {
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 1);
  EXPECT_THROW((void)parse_cli_axis_value("1..8@0", branches), std::invalid_argument);
  EXPECT_THROW((void)parse_cli_axis_value("1..8@-1", branches), std::invalid_argument);
}

TEST(CliAxis, GeomRangeAtSuffixRejectsMalformedK) {
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 1);
  EXPECT_THROW((void)parse_cli_axis_value("1..8@abc", branches), std::invalid_argument);
  EXPECT_THROW((void)parse_cli_axis_value("1..8@", branches), std::invalid_argument);
  EXPECT_THROW((void)parse_cli_axis_value("1..8@4x", branches), std::invalid_argument);
}

TEST(CliAxis, GeomRangeAtSuffixRejectsEmptyHi) {
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 1);
  EXPECT_THROW((void)parse_cli_axis_value("1..@4", branches), std::invalid_argument);
}

TEST(CliAxis, GeomRangeListSyntaxStillWorks) {
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 1);
  auto v = parse_cli_axis_value("100,250,500", branches);
  EXPECT_EQ(v, (std::vector<int64_t>{100, 250, 500}));
}
```

- [ ] **Step 2: Run tests; verify they fail**

```bash
cmake --build build --target test_cli_axis
ctest --test-dir build -R '^CliAxis\.GeomRange' --output-on-failure
```

Expected: all `CliAxis.GeomRange*` tests fail. Specifically:

- The positive `WithoutSuffixUsesAxisK` test fails because the current
  parser falls through `GeomRange` to the linear-range branch, producing
  101 elements for `"100..200"` instead of the geometric 5.
- The `AtSuffixOverridesAxisK` and `AtSuffixDensifies` tests fail with
  an uncaught `std::invalid_argument("not an integer: 8@1" / "2048@4")`
  from `parse_int` — `@` is currently just appended to the `hi` slice.
- The `AtSuffixOnLog2AxisThrowsWithSpecificMessage` and
  `AtSuffixOnLinearRangeAxisThrowsWithSpecificMessage` tests throw
  `std::invalid_argument`, but the message says `"not an integer"`
  rather than `"only valid for geom_range"`. The `EXPECT_NE(... find())`
  fires and the test fails.
- The malformed-`@` tests (`@0`, `@-1`, `@abc`, `@`, `@4x`, `1..@4`)
  all throw, but for `parse_int` reasons not for `@k` validator
  reasons. Plain `EXPECT_THROW` so these pass on either path — they
  exist to lock the post-impl behavior in.
- The `ListSyntaxStillWorks` test passes (literal-list parsing is
  unchanged), confirming the new tests don't accidentally regress the
  comma path.

Net: at least four `GeomRange*` tests fail unambiguously, which is
enough to drive the implementation in Step 3.

- [ ] **Step 3: Implement `@k` parsing**

Replace the `parse_cli_axis_value` function body in `src/cli_axis.cpp`
(everything from `std::vector<int64_t> parse_cli_axis_value(` through
its closing `}`) with:

```cpp
std::vector<int64_t> parse_cli_axis_value(const std::string& cli_value, const Axis& axis) {
  auto dotdot = cli_value.find("..");
  if (dotdot != std::string::npos) {
    std::string lo_s = cli_value.substr(0, dotdot);
    std::string hi_and_suffix = cli_value.substr(dotdot + 2);

    // Pull off the optional `@k` suffix before parsing hi.
    std::string hi_s = hi_and_suffix;
    bool has_at = false;
    int64_t k = 0;
    auto at = hi_and_suffix.find('@');
    if (at != std::string::npos) {
      has_at = true;
      hi_s = hi_and_suffix.substr(0, at);
      std::string k_s = hi_and_suffix.substr(at + 1);
      if (k_s.empty()) {
        throw std::invalid_argument("malformed @k suffix: " + cli_value);
      }
      if (axis.kind() != Axis::Kind::GeomRange) {
        throw std::invalid_argument("axis '" + axis.name() +
                                    "': @k is only valid for geom_range axes: " + cli_value);
      }
      k = parse_int(k_s);
      if (k <= 0) {
        throw std::invalid_argument("@k must be >= 1: " + cli_value);
      }
    }

    if (lo_s.empty() || hi_s.empty()) {
      throw std::invalid_argument("malformed range: " + cli_value);
    }
    int64_t lo = parse_int(lo_s);
    int64_t hi = parse_int(hi_s);
    if (hi < lo) {
      throw std::invalid_argument("range hi < lo: " + cli_value);
    }
    if (axis.kind() == Axis::Kind::GeomRange) {
      int64_t effective_k = has_at ? k : axis.samples_per_octave();
      return expand_geom_range(lo, hi, effective_k, cli_value);
    }
    if (axis.kind() == Axis::Kind::Log2Range) {
      return expand_log2_range(lo, hi, cli_value);
    }
    std::vector<int64_t> out;
    for (int64_t v = lo; v <= hi; ++v) {
      out.push_back(v);
    }
    return out;
  }

  std::vector<int64_t> out;
  std::stringstream ss(cli_value);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (item.empty()) {
      throw std::invalid_argument("empty value in list: " + cli_value);
    }
    int64_t v = parse_int(item);
    validate_value_against_kind(v, axis, cli_value);
    out.push_back(v);
  }
  if (out.empty()) {
    throw std::invalid_argument("no values: " + cli_value);
  }
  return out;
}
```

Also extend `validate_value_against_kind` so that a literal list passed
to a `GeomRange` axis is held to the same positivity contract as
`Log2Range`. Replace the function:

```cpp
void validate_value_against_kind(int64_t v, const Axis& axis, const std::string& cli_value) {
  if ((axis.kind() == Axis::Kind::Log2Range || axis.kind() == Axis::Kind::GeomRange) && v <= 0) {
    throw std::invalid_argument("axis '" + axis.name() + "' requires positive values: " + cli_value);
  }
}
```

- [ ] **Step 4: Run tests; verify they pass**

```bash
cmake --build build --target test_cli_axis
ctest --test-dir build -R '^CliAxis\.' --output-on-failure
```

Expected: all `CliAxis.*` tests pass — both the existing ones (no
regressions on `Log2Range`/`Range`/`Values` parsing) and the nine new
`CliAxis.GeomRange*` tests.

- [ ] **Step 5: Run the full test suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: `112/112 tests passed` (one platform-skip).

- [ ] **Step 6: Format + lint pass**

```bash
./scripts/format.sh
./scripts/lint.sh
```

Expected: both exit `0`.

- [ ] **Step 7: Commit**

```bash
git add src/cli_axis.cpp tests/test_cli_axis.cpp
git commit -m "feat(cli): parse lo..hi@k suffix for geom_range axes"
```

---

## Task 3: Wire `direct_branch_footprint` to `geom_range`

**Files:**
- Modify: `benchmarks/direct_branch_footprint.cpp`
- Modify: `tests/test_integration.cpp`

This task swaps the `branches` axis declaration. With `k=1` the default
sweep is byte-identical to today's (pinned by Task 1's first unit
test); the pre/post CSV diff captured during pre-flight serves as the
behavior-level regression check.

- [ ] **Step 1: Modify the axis declaration**

In `benchmarks/direct_branch_footprint.cpp`, replace the `axes()` method
body (around lines 74–79) with:

```cpp
  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::geom_range("branches", 1, 1 << 15, /*samples_per_octave=*/1),
        Axis::log2_range("spacing_bytes", 16, 128),
    };
  }
```

- [ ] **Step 2: Add a non-pow2 integration test**

Append to `tests/test_integration.cpp` (after
`Integration.DirectBranchFootprintSattoloPermuteHeaderAndRows`):

```cpp
TEST(Integration, DirectBranchFootprintGeomRangeNonPow2Sweep) {
  auto out = std::filesystem::temp_directory_path() / "ferret_btb_geom.csv";
  std::filesystem::remove(out);
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=1024..4096@4 --spacing_bytes=64"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  // Header + 9 data rows: expand_geom_range(1024, 4096, 4) lands on
  // {1024, 1218, 1448, 1722, 2048, 2435, 2896, 3444, 4096}; hi=4096 is
  // on the natural sequence so no hi-forcing.
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 10u);
  // No empty cells.
  EXPECT_EQ(contents.find(",,"), std::string::npos);
  // Spot-check one non-pow2 value appears as a row prefix in column
  // ordering "direct_branch_footprint,<branches>,<spacing_bytes>,...".
  EXPECT_NE(contents.find(",1218,64,"), std::string::npos);
  EXPECT_NE(contents.find(",2435,64,"), std::string::npos);
}
```

- [ ] **Step 3: Build and run integration tests**

```bash
cmake --build build
ctest --test-dir build -R '^Integration\.DirectBranchFootprint' --output-on-failure
```

Expected: all three `Integration.DirectBranchFootprint*` tests pass,
including the new one. If the new test fails on the `,1218,64,` /
`,2435,64,` assertion, the column order assumption is wrong; inspect
`/tmp/ferret_btb_geom.csv` and adjust the substring to match the actual
column layout (the first axis is the slowest-varying — by the project's
existing convention `branches` is column 2 right after `benchmark`).

- [ ] **Step 4: Re-run the equivalence check captured at pre-flight**

```bash
build/ferret run direct_branch_footprint \
  --branches=1..32768 --spacing_bytes=64 \
  --reps=3 --warmup=1 --seed=1 \
  --out=/tmp/geom-range-post.csv
diff <(cut -d, -f1-3 /tmp/geom-range-pre.csv) \
     <(cut -d, -f1-3 /tmp/geom-range-post.csv)
```

Expected: zero diff. Columns 1–3 are `benchmark,branches,spacing_bytes` —
identical row-for-row confirms the axis migration introduced no
behavior change at default settings. (Timing columns may differ
between runs and are intentionally excluded from the diff.)

- [ ] **Step 5: Run the full test suite**

```bash
ctest --test-dir build --output-on-failure
```

Expected: `113/113 tests passed` (one platform-skip). The count is one
higher than baseline because of the new integration test added in
Step 2.

- [ ] **Step 6: Format + lint pass**

```bash
./scripts/format.sh
./scripts/lint.sh
```

Expected: both exit `0`.

- [ ] **Step 7: Commit**

```bash
git add benchmarks/direct_branch_footprint.cpp tests/test_integration.cpp
git commit -m "feat(direct_branch_footprint): use geom_range for branches axis"
```

---

## Task 4: Documentation updates

**Files:**
- Modify: `docs/architecture.md`
- Modify: `docs/writing-a-benchmark.md`
- Modify: `README.md`

- [ ] **Step 1: Update `docs/architecture.md`**

Find the line:

```
- `axis.hpp` — declarative parameter axis (Range / Log2Range / Values)
  used by benchmarks to describe what to sweep.
```

Replace with:

```
- `axis.hpp` — declarative parameter axis (Range / Log2Range /
  GeomRange / Values) used by benchmarks to describe what to sweep.
```

- [ ] **Step 2: Update `docs/writing-a-benchmark.md`**

In the bullet that describes axes (the one starting with
`**axes()**`), replace:

```
- **`axes()`** — returns the swept axes. Order is significant: the
  first axis varies slowest in the CSV output and the first column to
  the right of `benchmark` is the first axis. Use
  `Axis::range(...)`, `Axis::log2_range(...)`, or
  `Axis::values(...)` (declared in `include/ferret/axis.hpp`).
```

with:

```
- **`axes()`** — returns the swept axes. Order is significant: the
  first axis varies slowest in the CSV output and the first column to
  the right of `benchmark` is the first axis. Use
  `Axis::range(...)`, `Axis::log2_range(...)`,
  `Axis::geom_range(name, lo, hi, samples_per_octave)`, or
  `Axis::values(...)` (declared in `include/ferret/axis.hpp`).
  `geom_range` is `log2_range` when `samples_per_octave == 1`; pick
  a larger `k` when the capacity cliff under test sits between two
  adjacent powers of two and you want denser default sampling.
```

- [ ] **Step 3: Update `README.md` CLI section**

In the `### CLI flags` block, replace the line:

```
  --<axis>=lo..hi   range using the axis's declared step policy
```

with the two lines:

```
  --<axis>=lo..hi   range using the axis's declared step policy
  --<axis>=lo..hi@k geom_range axes only: override samples-per-octave to k
```

- [ ] **Step 4: Format pass**

```bash
./scripts/format.sh
```

Expected: exit `0`. (Format only — lint runs are C++-focused and
docs-only changes won't affect them.)

- [ ] **Step 5: Sanity build (no functional change, but verify nothing tripped a doc-link check)**

```bash
ctest --test-dir build --output-on-failure
```

Expected: `113/113 tests passed`.

- [ ] **Step 6: Commit**

```bash
git add docs/architecture.md docs/writing-a-benchmark.md README.md
git commit -m "docs(axis): document geom_range and lo..hi@k CLI suffix"
```

---

## Post-flight

- [ ] **Final full-suite check**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
./scripts/lint.sh
```

Expected: `113/113 tests passed`, lint exits `0`.

- [ ] **Sanity-run a denser sweep end-to-end**

```bash
build/ferret run direct_branch_footprint \
  --branches=512..8192@4 --spacing_bytes=64 \
  --reps=3 --warmup=1 \
  --out=/tmp/btb-geom-demo.csv
wc -l /tmp/btb-geom-demo.csv
head -2 /tmp/btb-geom-demo.csv
```

Expected: 17 lines (header + 16 data rows; `expand_geom_range(512, 8192, 4)`
spans 4 octaves at 4 samples/octave = 16 distinct integer points after
dedup, with the natural sequence landing on `hi=8192`). Header column 2
is `branches`; spot-check the first data row reports `branches=512`.

- [ ] **Review commit log**

```bash
git log --oneline worktree-feat+geom-range-axis ^origin/main
```

Expected four feature commits (Tasks 1–4), each touching a coherent
subset of files, plus the design-spec commit (`bd027c2`) at the base.

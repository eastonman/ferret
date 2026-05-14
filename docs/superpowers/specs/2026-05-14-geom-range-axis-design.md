# Non-Power-of-Two Branch Counts via `geom_range` Axis — Design Spec

**Date:** 2026-05-14
**Status:** Design — pending implementation plan
**Baseline:** `b0f01f6` (`main`)
**Working branch:** set by the implementation plan (created via `using-git-worktrees`)

## Goal

Let users sweep the `direct_branch_footprint` `branches` axis at finer
granularity than powers of two, so the BTB capacity curve has resolution
between adjacent octaves without resorting to a hand-curated literal
value list on every invocation.

The feature ships as a new general-purpose axis kind, `Axis::GeomRange`,
so any future capacity-cliff benchmark can use the same primitive.

## Motivation

Concrete signals on the current state:

- `benchmarks/direct_branch_footprint.cpp:76` declares
  `Axis::log2_range("branches", 1, 1 << 15)`. The default sweep is 16
  points across 15 octaves — one sample per octave.
- The BTB capacity cliff observed in `btb.png` / `btb.csv` falls between
  two adjacent pow2 points, so the user cannot localize the cliff from
  the default sweep alone.
- The CLI already accepts literal lists (`--branches=900,1100,1300,...`)
  for arbitrary values, but composing a geometrically-spaced list by
  hand is tedious and error-prone, and the default sweep is the artifact
  most users actually look at.

## Non-goals

These are explicitly out of scope so the diff stays focused:

- **No change to `Log2Range` semantics or `expand_log2_range` behavior.**
  Existing callers — including every other benchmark — keep their
  current expansion exactly.
- **No change to other benchmarks' default axes.** Only
  `direct_branch_footprint`'s `branches` axis migrates to `GeomRange`,
  and it migrates with `samples_per_octave=1` so the default sweep is
  bit-for-bit identical.
- **No change to the JIT kernel emitter.** `branches` is already used as
  a count of sites, not as a power-of-two index.
- **No regeneration of committed `btb.csv` / `btb.png` in this change.**
  Those refresh organically when users re-run the benchmark with `@k`.
- **No plot-script changes.** The CSV row format is unchanged.
- **No new CLI flag.** Sweep shape is governed by the axis declaration
  plus the existing `lo..hi` range token, extended with an `@k` suffix.

## What the user types

```
# default sweep — unchanged from today
ferret run direct_branch_footprint

# densify the entire branches sweep to 4 samples per octave
ferret run direct_branch_footprint --branches=1..32768@4

# zoom into the BTB cliff region with 8 samples per octave
ferret run direct_branch_footprint --branches=1024..4096@8

# hand-curated literal list still works
ferret run direct_branch_footprint --branches=900,1100,1300,1600
```

## Approach

Add a new `Axis::Kind::GeomRange` alongside the existing `Range`,
`Log2Range`, and `Values` kinds. `GeomRange` carries a
`samples_per_octave` field. Expansion is a single geometric sequence
rounded to integers, with adjacent-duplicate dedup and a forced final
point at `hi`. `k=1` reproduces `Log2Range` byte-for-byte, so migrating
`direct_branch_footprint`'s declaration is a no-op at default settings.

CLI parsing for `lo..hi` range tokens learns one new suffix: `@k`, which
overrides the axis's declared `samples_per_octave`. `@k` is rejected on
non-`GeomRange` axes — mixing semantics silently would be a footgun.

## Target file layout

```
include/ferret/
  axis.hpp          [+]   GeomRange kind, geom_range factory,
                          samples_per_octave accessor,
                          expand_geom_range free function
src/
  axis.cpp          [+]   GeomRange branch in expand(), expand_geom_range
  cli_axis.cpp      [+]   @k suffix parsing in the lo..hi branch
benchmarks/
  direct_branch_footprint.cpp [+]   axis declaration switches to
                                    Axis::geom_range(..., 1)
tests/
  test_params_axis.cpp [+]   geom_range expansion cases
  test_cli_axis.cpp    [+]   @k parsing cases (positive, error)
  test_integration.cpp [+]   non-pow2 branches end-to-end smoke
docs/
  architecture.md      [+]   GeomRange in axis kind list
  writing-a-benchmark.md [+] geom_range example
README.md            [+]   @k CLI suffix documentation
```

## Public API additions

```cpp
// include/ferret/axis.hpp

enum class Kind { Range, Log2Range, GeomRange, Values };

// Geometric sweep with k samples per octave between lo and hi inclusive.
// Equivalent to log2_range when samples_per_octave == 1. See
// expand_geom_range below for exact semantics.
//
// Throws std::invalid_argument when lo <= 0, hi < lo, or
// samples_per_octave <= 0.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
static Axis geom_range(std::string name, int64_t lo, int64_t hi,
                       int64_t samples_per_octave);

// Returns the declared samples_per_octave for GeomRange axes, or 1 for
// other kinds. Used by the CLI to fall back when no @k suffix is given.
int64_t samples_per_octave() const;

// Generates {round(lo * 2^(i/k))} for i = 0, 1, ... while the value
// <= hi; result is deduplicated against the previously appended value;
// hi is appended as the final point when the generated sequence does
// not naturally land on it. k=1 reproduces expand_log2_range exactly.
// Throws std::invalid_argument when lo <= 0, hi < lo, or k <= 0.
// `context` is prepended to the error message.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<int64_t> expand_geom_range(int64_t lo, int64_t hi, int64_t k,
                                       std::string_view context = {});
```

The `Axis` struct gains one `int64_t k_ = 1` field. `expand()` dispatches
to `expand_geom_range(lo_, hi_, k_, "Axis '" + name_ + "'")` for the new
kind. Existing kinds are untouched.

## `expand_geom_range` semantics

```
expand_geom_range(lo, hi, k):
  validate lo > 0, hi >= lo, k >= 1
  out := []
  for i = 0, 1, 2, ...:
    v := llround(lo * 2^(i/k))
    if v > hi:
      break
    if out is non-empty and v == out.back():
      continue          // adjacent-duplicate dedup
    out.push_back(v)
  if out.empty() or out.back() < hi:
    out.push_back(hi)
  return out
```

Worked examples (these are the test fixtures):

| `(lo, hi, k)`         | Output                                                              | Notes |
| --------------------- | ------------------------------------------------------------------- | ----- |
| `(1, 32768, 1)`       | `{1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768}` | byte-identical to `log2_range(1, 32768)` |
| `(1, 8, 4)`           | `{1, 2, 3, 4, 5, 6, 7, 8}`                                          | dedup absorbs the rounded duplicates at the low end |
| `(1024, 2048, 4)`     | `{1024, 1218, 1448, 1722, 2048}`                                    | the typical "zoom into one octave" use case |
| `(1, 10, 1)`          | `{1, 2, 4, 8, 10}`                                                  | `hi` forced as the final point |
| `(5, 5, 4)`           | `{5}`                                                               | single-point axis |

Overflow guard: stop if the next multiplication would land beyond
`INT64_MAX`. With `lo <= INT64_MAX` and the `v > hi` exit this is
unreachable in practice; the guard mirrors the one in
`expand_log2_range` for defense in depth.

## CLI grammar extension

`src/cli_axis.cpp::parse_cli_axis_value` already special-cases the `..`
token. The new behavior:

1. After splitting on `..`, scan the `hi` substring for `@`. If present:
   1. Split `hi` into `hi_str` and `k_str` at `@`.
   2. Parse `k_str` as `int64_t`. Reject `k <= 0` and trailing junk.
   3. Require the axis kind to be `GeomRange`; otherwise throw
      `std::invalid_argument("axis '<name>': @k is only valid for geom_range axes")`.
   4. Reject empty `hi_str` (e.g. `1..@4`).
   5. Call `expand_geom_range(lo, hi, k, cli_value)`.
2. If no `@`:
   1. `Log2Range` → `expand_log2_range` (unchanged).
   2. `GeomRange` → `expand_geom_range(lo, hi, axis.samples_per_octave(), cli_value)`.
   3. `Range` → linear iteration (unchanged).

Literal-list parsing (`100,250,500`) is untouched; it remains the
escape hatch for any axis kind.

## Benchmark wiring

```cpp
// benchmarks/direct_branch_footprint.cpp:74-79
[[nodiscard]] SweepAxes axes() const override {
  return {
      Axis::geom_range("branches", 1, 1 << 15, /*samples_per_octave=*/1),
      Axis::log2_range("spacing_bytes", 16, 128),
  };
}
```

`samples_per_octave=1` makes the default sweep equal to today's. The
kernel emitter at `emit_kernel` does not need to change: it treats
`branches` as a plain count and allocates `labels`, `jumps`, and the
permutation by exactly that count.

## Tests

`tests/test_params_axis.cpp` adds:
- `Axis::geom_range(1, 32768, 1).expand()` equals
  `Axis::log2_range(1, 32768).expand()`.
- `Axis::geom_range(1, 8, 4).expand() == {1, 2, 3, 4, 5, 6, 7, 8}`.
- `Axis::geom_range(1024, 2048, 4).expand() ==
  {1024, 1218, 1448, 1722, 2048}`.
- `Axis::geom_range(1, 10, 1).expand() == {1, 2, 4, 8, 10}` —
  hi-forcing.
- `Axis::geom_range(5, 5, 4).expand() == {5}` — single point.
- Throws `std::invalid_argument` on `lo <= 0`, `hi < lo`, `k <= 0`.

`tests/test_cli_axis.cpp` adds:
- `--branches=1..32768@4` against a `geom_range` axis returns the
  expansion produced by `expand_geom_range(1, 32768, 4)`.
- `--branches=1..32768@1` against a `geom_range` axis equals
  `expand_log2_range(1, 32768)`.
- `--branches=1..32768@4` against a `log2_range` axis throws with the
  "only valid for geom_range axes" message fragment.
- `--branches=1..32768@4` against a `range` axis throws with the same
  fragment.
- `@0`, `@-1`, `@abc`, `1..@4`, `1..32768@` all throw.
- `--branches=100,250,500` still parses to literal `{100, 250, 500}`.

`tests/test_integration.cpp` adds:
- A smoke test that runs `direct_branch_footprint` end-to-end with
  `--branches=1024..4096@4`, asserts the CSV contains nine rows whose
  `branches` column is
  `{1024, 1218, 1448, 1722, 2048, 2435, 2896, 3444, 4096}` (the natural
  expansion of `expand_geom_range(1024, 4096, 4)`; the final point
  lands on `hi` exactly so no hi-forcing fires), and that each row has
  a finite `ticks_median`.

## Documentation

- `docs/architecture.md:29` — extend the `axis.hpp` line to mention
  `GeomRange`.
- `docs/writing-a-benchmark.md` — add one paragraph and a one-liner
  example showing `Axis::geom_range("branches", 1, 1 << 15, 4)` for a
  benchmark that wants denser default sampling.
- `README.md` CLI section — one line documenting `lo..hi@k` and the
  fact that `@k` is only valid for `geom_range` axes.

## Risk surface

- **Floating-point determinism across platforms.** `std::exp2` and
  `std::llround` are well-defined for the integer arguments we feed
  them, but cross-platform rounding can in theory disagree at half-way
  points. Mitigation: the test fixtures pin exact expected outputs for
  the worked examples; any platform that diverges fails the test and
  we'll revisit (likely by switching to an integer-only formulation
  such as Bresenham-style accumulation).
- **`Log2Range` parity.** The `k=1` parity claim is asserted directly
  by the first unit test; any drift fails CI.
- **CLI grammar conflicts.** `@` is not currently meaningful anywhere
  in `parse_cli_axis_value`; the literal-list path uses `,` and the
  range path uses `..`. No collision.

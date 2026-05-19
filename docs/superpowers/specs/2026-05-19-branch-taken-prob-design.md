# `branch_history_footprint` — taken-probability knob

Date: 2026-05-19
Status: brainstorming output, pending user review before plan generation.

## 1. Goal

Add a per-branch direction-probability knob to `branch_history_footprint`.
Today the benchmark has `--pattern=0|1`: `0` is an all-not-taken fill
(trivial baseline) and `1` is an iid 50/50 random fill. The 50/50 point
is the maximum-entropy worst case for the direction predictor, but
there is no way to study the full **predictability sweep** — how
per-branch cost varies as each branch's bias moves from always-not-taken
through max-entropy 50/50 toward always-taken.

This spec replaces the binary `pattern` option with a probability
percent in `[0, 100]`, with the random fill drawing each cell from an
independent Bernoulli(`p`).

## 2. Scope

### In scope

- Replace the existing `pattern` option with `taken_prob_pct`
  (int, `[0, 100]`, default `50` — preserves today's `--pattern=1`
  behavior).
- Rewrite `generate_pattern_fill` to draw each cell as
  `rng() % 100 < taken_prob_pct` (independent Bernoulli per cell).
- Update validation: reject `< 0` or `> 100` with
  `std::invalid_argument` before any compiler-state mutation, matching
  the existing pre-codegen validation pattern.
- Update unit tests: replace the two `pattern` tests with
  `taken_prob_pct=0/100` (deterministic-fill) tests; add a 50/50
  determinism+seed-divergence test (already exists, just retarget); add
  a coarse distribution sanity test asserting the empirical taken-rate
  is within a wide tolerance at `taken_prob_pct=50` for a large fill.
- Update `docs/benchmarks/branch_history_footprint.md` option table,
  caveats, and the "reading the curves" paragraph (a `taken_prob_pct`
  sweep gives a U-shape in mispredict cost).

### Out of scope

- **Markov-correlated sequences** (the M2 alternative from
  brainstorming). Genuinely orthogonal predictor axis, but adds a
  second knob and per-branch state. Defer until there's a study
  motivating it.
- **Per-branch bias mixture** (M3). The existing `branches` axis
  already covers most of what a per-branch bias spread would buy.
- **Sub-percent resolution.** Percent is good enough for sweeping
  predictor U-curves; `Params` is `int64_t`-only and pushing to
  per-mille would change ergonomics for negligible gain.
- **Backwards compatibility for the old `--pattern` flag.** Repo is
  pre-1.0 single-author; the CLI/CSV column rename is cheap.
- Changes to any other benchmark. `direct_branch_footprint` uses
  unconditional jumps and doesn't have a direction fill;
  `dependent_chain_throughput` and `nested_call_depth` are unaffected.

## 3. Surface changes

### CLI

| flag                      | old                              | new                                                                  |
| ------------------------- | -------------------------------- | -------------------------------------------------------------------- |
| `--pattern=0\|1`          | `0`=all-zero, `1`=random 50/50   | **removed**                                                          |
| `--taken_prob_pct=N`      | —                                | `N` in `[0,100]`. `0`=always-not-taken, `100`=always-taken, default `50`. |

Mapping for existing users: `--pattern=0` → `--taken_prob_pct=0`;
`--pattern=1` (or omitted) → `--taken_prob_pct=50` (also the new
default, so omitting still gives identical behavior).

### CSV

Column `pattern` is replaced by column `taken_prob_pct`. Values are
integer percent. Same position in the CSV (per-benchmark options follow
sweep axes in the order returned by `options()`).

### Bonus reachable by new surface

- `--taken_prob_pct=100` exposes an always-taken baseline that the old
  `pattern` flag couldn't produce. Useful as a second control surface
  alongside `--taken_prob_pct=0` for confirming both trivial-prediction
  endpoints flatten the heatmap.

## 4. Algorithm

```cpp
// inside generate_pattern_fill, after seed mixing:
std::mt19937_64 rng(mixed);
for (auto& v : flat) {
  v = (rng() % 100U) < static_cast<uint64_t>(taken_prob_pct) ? 1U : 0U;
}
```

Modulo bias on `uint64_t % 100` is on the order of 2⁻⁵⁸ — negligible
for predictor workload statistics. `std::bernoulli_distribution` would
also work; chose modulo for one-line readability and zero floating
point in the fill loop.

### Edge cases (no special-casing needed)

- `taken_prob_pct == 0`: `rng() % 100 < 0` is always false → all zeros.
- `taken_prob_pct == 100`: `rng() % 100 < 100` is always true → all ones.

Both fall out of the same loop body — no separate code paths.

## 5. Files touched

- `benchmarks/branch_history_footprint.cpp`
  - `options()`: rename entry, change default `1 → 50`.
  - `emit_kernel`: rename local, update validation message, update the
    fill call site.
  - `generate_pattern_fill`: rename `pattern` param to
    `taken_prob_pct`, rewrite loop body, drop the `pattern == 0`
    early-return (now handled by the generic loop).
- `tests/test_branch_history_footprint.cpp`
  - Update `make_params` default and option-test expectations.
  - Replace `RejectsInvalidPattern` with `RejectsOutOfRangeProbability`
    (covers `-1` and `101`).
  - Replace `ZeroPatternProducesAllZeros` with
    `ZeroProbabilityProducesAllZeros`.
  - Add `HundredProbabilityProducesAllOnes`.
  - Add `MidProbabilityRoughlyHalfTaken` — generate a 100×100 fill
    (10 000 cells) at `taken_prob_pct=50` with a fixed seed, assert
    empirical taken-rate within `[0.45, 0.55]` (well outside the
    1σ ≈ 0.005 noise floor for `n=10 000`).
  - Retarget existing seed-determinism / seed-divergence /
    point-divergence tests to the new option name; semantics
    unchanged.
- `docs/benchmarks/branch_history_footprint.md`
  - Option table: replace the two `pattern` rows with one
    `taken_prob_pct` row.
  - CLI surface table: same swap.
  - "Reading the curves" paragraph: replace the `--pattern=0` control
    sentence with the symmetric `--taken_prob_pct=0` / `=100` pair, and
    add a brief note that a `taken_prob_pct` sweep at fixed
    `(branches, history_len)` traces a U-shape.

No changes to `Params`, `BenchOption`, the runner, the registry, CSV
plumbing, or `direct_branch_footprint`.

## 6. Validation & testing

- Pre-codegen validation throws `std::invalid_argument` before any
  `sljit_emit_*` call, consistent with the existing `branches`,
  `history_len`, and `spacing_bytes` checks.
- Unit tests cover: option default, range rejection (both ends),
  endpoint determinism (0 and 100), seed determinism, seed divergence,
  param-point divergence, and a coarse middle-probability empirical
  check.
- Integration: no new integration test required; existing
  `test_integration.cpp` coverage of the benchmark continues to use
  the default option value.
- Manual: run
  `build/ferret run branch_history_footprint --taken_prob_pct=0,25,50,75,100 --branches=64 --history_len=256 --out=/tmp/bhp.csv`
  and eyeball the U-shape in the per-site cost column.

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

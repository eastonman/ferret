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

```text
   PC                  site (>= spacing_bytes apart)
 0x0000   ┌──────────────────────────────────────┐
          │  MOV  r2, [row_ptr + 0]              │  load 32-bit pattern
          │  CMP  r2, 0                          │
          │  JNE  .Lnext_0                       │ ──┐  branch to next instr
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
          │  CMP  hist_idx, history_len  → wrap  │
          │  SUBS iters, iters, 1; B.NE loop_top │
          └──────────────────────────────────────┘
```

Annotated:

- The pattern buffer is laid out as `flat[history_len][branches]`
  (transposed). Consecutive branch sites in one outer iteration
  access adjacent words in memory; one L1 line covers 16 sites.
- Sites are emitted via sljit's high-level ops (load, cmp, conditional
  jump to the immediately-following label) and padded with NOPs so
  each site occupies _at least_ `spacing_bytes`. The actual stride may
  be a few bytes larger when sljit picks longer encodings — `spacing`
  is a minimum, not an exact value. Its job is to spread sites across
  enough BTB sets to avoid conflict aliasing dominating the curve.
- Per-branch displacement is baked as `j*4` into the load — no
  per-site address ALU. `row_ptr = flat_base + hist_idx * branches * 4`
  is recomputed once per outer iteration; the chain itself never
  mutates the address base.

## Per-benchmark options

| flag                 | meaning                                                   |
| -------------------- | --------------------------------------------------------- |
| `--pattern=0`        | All-zero fill — all-not-taken trivial baseline.           |
| `--pattern=1` (def.) | Per-entry random `{0,1}` seeded by `--seed`.              |
| `--spacing_bytes=16` | Minimum PC stride per site. Min 8 (AArch64) / 6 (x86_64). |

## CLI surface

| flag                     | meaning                                         |
| ------------------------ | ----------------------------------------------- |
| `--branches=A..B`        | Geometric sweep, default `k=1`, e.g. `1..512`.  |
| `--branches=A..B@k`      | Geometric sweep with `k` samples per octave.    |
| `--branches=v1,v2,…`     | Explicit list.                                  |
| `--history_len=A..B[@k]` | Same syntax as `--branches`. Default `4..4096`. |
| `--pattern=0\|1`         | See above. Default `1`.                         |
| `--spacing_bytes=N`      | Minimum per-site PC stride. Default 16.         |
| `--seed=…`               | Seeds the per-branch random fill.               |

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
  section — probe and benchmark land on _some_ P-core, not
  necessarily the same one.

## Related docs

- Construction rationale: [design spec](../superpowers/specs/2026-05-17-branch-history-footprint-design.md).
- Project two-step workflow: [project README](../../README.md).

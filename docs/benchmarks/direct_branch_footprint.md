# `direct_branch_footprint` — direct-jump BTB capacity

`N` unconditional direct branches at PC = base + i × spacing,
chained so exactly `N` branches execute per outer-loop iteration.
The outer loop amortizes the runner's tick-read overhead.

The per-site cost stays flat while `N` ≤ direct-BTB capacity; once
`N` exceeds it, mispredicts compound and per-branch cost steps up.
The cliff position is the direct-jump BTB capacity.

## Kernel structure

```text
   PC                  site
 0x0000   ┌───────────────────────────┐
          │  B   target_0             │ ──┐
          │  <NOP pad to spacing>     │   │
 base+1×spacing   ┌───────────────────┐   │  spacing_bytes (default 64)
          │  B   target_1             │ ──┼─┐
          │  <NOP pad to spacing>     │   │ │
 base+2×spacing   ┌───────────────────┐   │ │
          │  B   target_2             │ ──┼─┼─┐
          │   ...                     │   │ │ │
          ├───────────────────────────┤   │ │ │
          │  exit_label  ──── SUB,JNZ │   │ │ │
          │              ── loop_top  │   │ │ │
          └───────────────────────────┘   │ │ │
                                          │ │ │
   sattolo_permute=0 (default):           │ │ │
     target_i = site_{i+1}    ◄───────────┘─┘─┘   sequential fall-through
   sattolo_permute=1:
     target_i = π(i),  π = Sattolo cycle         single Hamiltonian cycle
                                                 (breaks I-cache spatial prefetch)
```

Annotated:

- Each site is `kJumpBytes` of branch encoding plus NOP padding to
  `spacing`: 4 + (spacing − 4) on AArch64; 5 + (spacing − 5) on
  x86_64. Layout is verified post-codegen by `verify_layout()`.
- The exit label (`labels[branches]` in the source) is the
  post-chain return point: the chain falls through there exactly
  once per iteration, the outer loop decrements and branches back to
  `loop_top`.
- With `sattolo_permute=1` the unique cycle edge pointing back to
  `labels[0]` is rerouted to the exit label so each iteration still
  executes exactly `N` branches (Sattolo's algorithm; see
  `include/ferret/permute.hpp`).

## Per-benchmark options

`--sattolo_permute=0|1` (default `0`).

- `0`: wires each branch to fall through to the next in layout order.
  Sequential PC walk — measures BTB _plus_ whatever sequential
  prefetch the front-end does.
- `1`: rewires the jump targets as a uniform random Hamiltonian
  cycle over the same `N` branches (Sattolo's algorithm, seeded by
  `--seed` mixed with `branches` and `spacing_bytes`). `N` branches
  still execute per iteration but the executed PC order is
  unpredictable — useful for isolating the BTB contribution from
  sequential-prefetch and I-cache spatial-locality effects.

## CLI surface

| flag                     | meaning                                                           |
| ------------------------ | ----------------------------------------------------------------- |
| `--branches=A..B`        | Geometric sweep, default `k=1` (same as log₂), e.g. `1..1024`.    |
| `--branches=A..B@k`      | Geometric sweep with `k` samples per octave, e.g. `1024..4096@4`. |
| `--branches=v1,v2,…`     | Explicit list.                                                    |
| `--spacing_bytes=A..B`   | Log₂ sweep over `{16, 32, 64, 128}`. Site stride.                 |
| `--sattolo_permute=0\|1` | See above. Default `0`.                                           |
| `--seed=…`               | Seeds the Sattolo cycle (mixed with branches/spacing).            |

See [`../cli.md`](../cli.md) for global flags.

## Reading the curves

Plot `cycles_per_site` vs. `branches` on a log-x axis. Below the BTB
capacity the curve is flat near the front-end's branch-per-cycle
limit. Past the capacity, per-branch cost climbs as more sites miss
in the BTB.

`sattolo_permute=1` typically lowers the apparent capacity (because
the predictor can no longer ride the sequential pattern) and
sharpens the cliff.

## Caveats

- **Outer-loop pollution.** The decrement + back-edge at the exit
  label add one indirect branch worth of overhead per iteration.
  Divided across `N` sites it's negligible for `N ≥ 64`, but
  smaller `N` curves carry a measurable per-iteration tax.
- **`spacing_bytes < kJumpBytes` rejected.** On x86_64 each branch
  is 5 B; `spacing_bytes` ≥ 5 (must also be `kBranchAlign`-aligned,
  which is 1 on x86_64 and 4 on AArch64).
- **Apple Silicon pinning.** See the project README's discipline
  section — probe and benchmark land on _some_ P-core, not
  necessarily the same one.

## Related docs

- Construction rationale: [`writing-a-benchmark.md`](../writing-a-benchmark.md) (worked example B).
- Project two-step workflow: [project README](../../README.md).

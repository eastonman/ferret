# `store_load_distance` — how far apart a store and its load may sit

A serial dependent chain whose store and load are pushed a controlled
number of instructions apart. Per-link cost is the store-to-load forward
latency plus `alu_ops`; subtract `alu_ops` to read the forward cost on
its own.

Where the curve returns to zero measures the width of the window the
mechanism needs the pair to fall _outside_ of.

## Kernel structure

```text
  loop_top:
    ┌─ one link, repeated to fill a constant instruction budget ─┐
    │   str  x0, [base, #slot]              ◄── chain in         │
    │   <separation filler instructions>                         │
    │   ldr  x3, [base, #slot]                                   │
    │   add  x0, x3, #1                                          │
    │   <alu_ops-1 further adds>            ────► chain out      │
    └────────────────────────────────────────────────────────────┘
    SUB counter, 1 ; JNZ loop_top
```

The dependency is `add → str` data `→ ldr` result `→ add`, unbroken.
Nothing else is on the critical path: the address is `base + immediate`,
available immediately.

Annotated:

- **The loop body is sized to a constant instruction count, not a
  constant link count.** Otherwise sweeping `separation` would also
  sweep the L1I footprint, and instruction-cache pressure would
  masquerade as a distance effect.
- **`alu_ops` defaults to 16**, unusually high, because it buys a
  latency shadow for the filler work to hide in. Without it the row
  stops measuring latency and starts measuring issue width.
- **Every filler kind is exactly one instruction**, so `separation` is
  both an instruction count and the emitted byte stride ÷ 4.
  `verify_layout` asserts this with strict equality per link.

## Per-benchmark options

`--alu_ops=N` (default `16`, minimum `1`). Latency budget, and the
calibration knob: cost must move by exactly one cycle per added op. A
slope that isn't 1.0 means the chain is not serial and nothing else in
the row means what it claims.

## CLI surface

| flag                 | meaning                                                                 |
| -------------------- | ----------------------------------------------------------------------- |
| `--separation=A..B`  | Contiguous range, default `0..16`. Instructions between store and load. |
| `--separation=v1,v2` | Explicit list.                                                          |
| `--filler=v1,v2,…`   | `0` alu, `1` nop, `2` store, `3` load, `4` branch, `5` base_write.      |
| `--alu_ops=N`        | See above.                                                              |

See [`../cli.md`](../cli.md) for global flags.

### Filler kinds

| value | kind         | what it asks                                                  |
| ----- | ------------ | ------------------------------------------------------------- |
| `0`   | `alu`        | independent ADD, rotating destinations — the neutral baseline |
| `1`   | `nop`        | occupies a decode slot and nothing else                       |
| `2`   | `store`      | independent store, distinct address per position              |
| `3`   | `load`       | independent load from a never-stored region                   |
| `4`   | `branch`     | taken branch to the next instruction — ends a fetch block     |
| `5`   | `base_write` | rewrites the address base register **with its own value**     |

`base_write` is the odd one out: the address is unchanged, only the
architectural register is written. Those rows address the chain slot
through a general register, since the stack pointer is not ours to
rewrite. If a mechanism keyed on the register _name_ has to invalidate
when that name is written, this is where it shows.

Rotating the filler destination registers matters. A single shared
destination makes load fillers look artificially cheap and skews the
whole curve — an easy mistake that produces a plausible-looking wrong
answer.

## Reading the curves

Subtract `alu_ops` from `cycles_per_site`.

- **A minimum at separation 0 that does _not_ continue at 1** means two
  distinct mechanisms: one for immediately adjacent pairs, another that
  needs them further apart.
- **A linear ramp back toward zero** is the signature of a window the
  pair must escape. If the cost fits `P × (1 − d/W)` for
  `d = separation + 1`, then the x-intercept is `W` — the width of that
  window in instructions — and `P` is the fallback penalty.
- **`alu`, `nop`, `store` and `load` agreeing** means only the slot
  count matters, not what occupies it. Any kind that deviates is doing
  something structural — a taken branch ending a fetch block, for
  instance.
- **`base_write` pinned near the fallback at every separation** means
  writing the base register invalidates the match.

## Caveats

- **The chain must never load a repeating value.** A per-PC last-value
  load predictor will sever the chain and the row will report issue
  throughput as a latency -- roughly 0.4 cycles per link instead of the
  real number. The `alu_ops` carry increment is what keeps every load
  result fresh; see the dependent-chain section of
  [`../writing-a-benchmark.md`](../writing-a-benchmark.md) and validate
  with `scripts/chain_slope.py`.
- **The first parameter point of a sweep reads high with `--warmup=1`.**
  It shows up as a spurious spike at `separation=0`. Use `--warmup=2` or
  more; the CI runner does.
- **`separation` is swept linearly, not geometrically.** The structure
  is a handful of instructions wide, so a log2 axis would step straight
  over it. Plots default to a linear x-axis for the same reason.
- **This benchmark does not probe capacity.** The chain is serial, so
  exactly one store/load pair is in flight. How many pairs can be
  tracked at once needs concurrently live chains — a different kernel.
- **Large separations with memory fillers are rejected.** Filler stores
  and loads walk one 128-byte line per position, so they eventually
  leave the single-instruction addressing window; the point is rejected
  rather than silently measured with an extra address computation.
- **A rejected point aborts the whole sweep**, it is not skipped.
- **The layout check is AArch64-only** — x86_64 instruction lengths vary,
  so a uniform per-link stride does not hold there by construction.
- **Apple Silicon pinning.** See the project README's discipline section.

## Related docs

- Companion benchmarks: [`store_load_footprint.md`](store_load_footprint.md),
  [`store_load_overlap.md`](store_load_overlap.md).
- Construction rationale: [`writing-a-benchmark.md`](../writing-a-benchmark.md).

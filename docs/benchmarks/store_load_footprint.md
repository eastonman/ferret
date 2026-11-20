# `store_load_footprint` — store-to-load forward latency

A strictly serial dependent chain threaded through memory. Each link
stores the chain value to a stack slot, reads it straight back, and
carries it to the next link through an ALU op. The per-link cost is the
machine's store-to-load forward latency plus `alu_ops`.

A core that forwards through the store queue pays its full forwarding
latency per link. A core that resolves the load at rename time — memory
renaming — pays close to nothing, and the chain collapses to little more
than the ALU op.

## Kernel structure

```text
  local area:  addresses × stride_bytes bytes of sljit stack locals
               (SLJIT_SP + 0, + stride, + 2×stride, …)

  loop_top:
    ┌─ rotation r = 0 … repeats-1 ──────────────────────────────┐
    │   i = 0:   str  x0, [base, #0]              ◄── chain in  │
    │            ldr  x3, [base, #0]                            │
    │            add  x0, x3, #1                                │
    │   i = 1:   str  x0, [base, #1×stride]                     │
    │            ldr  x3, [base, #1×stride]                     │
    │            add  x0, x3, #1                                │
    │            …                                              │
    │   i = N-1: str  x0, [base, #(N-1)×stride]                 │
    │            ldr  x3, [base, #(N-1)×stride]                 │
    │            add  x0, x3, #1              ────► chain out   │
    └───────────────────────────────────────────────────────────┘
    SUB counter, 1 ; JNZ loop_top

  base_reg selects the register NAME each side uses for the one address:
    0   str [sp,#i]  / ldr [sp,#i]
    1   str [xN,#i]  / ldr [xN,#i]
    2   str [sp,#i]  / ldr [xN,#i]      <- same bytes, different name
    3   str [xN,#i]  / ldr [sp,#i]
  xN comes from sljit_get_local_base and provably holds exactly SP;
  it is hoisted out of the loop so all four forms emit an identical
  instruction sequence and differ only in the encoded register field.
```

The dependency is `add → str` data `→ ldr` result `→ add`, unbroken
across every link and every rotation. Nothing else in the link is on the
critical path: both addresses are `base + immediate`, available
immediately.

Annotated:

- **The load targets a different register than the store's source.**
  Sharing one register would leave the chain value constant for the
  whole run and make a same-register peephole indistinguishable from a
  genuinely fast forward. `alu_ops=0` is rejected for this reason — it
  would force the two registers to coincide, which is a different
  experiment. On an M4 Pro that variant measures ~4.8 cycles per link
  against ~2.0 for `alu_ops=1`.
- **The rotation repeats to a fixed 512 links per iteration**, so
  `addresses=1` isn't three instructions drowning in loop overhead. Both
  ends of the axis do identical total work.
- **Layout is verified post-codegen.** On AArch64 `verify_layout()`
  asserts every link is exactly `(2 + alu_ops) × 4` bytes. sljit
  silently prepends an `ADDI` to re-base once an offset leaves the
  single-instruction window, which would inflate per-link latency by a
  whole instruction with no other symptom.

## Per-benchmark options

`--alu_ops=N` (default `1`, minimum `1`).

Number of ALU ops carrying the loaded value to the next link. This is a
calibration knob, not a tuning knob: since each op is a 1-cycle
dependent `ADD`, fitting cycles-per-link against `alu_ops` must give a
slope of exactly 1.0, and the intercept is the forward latency. **A
slope that isn't 1.0 means the chain is not serial and nothing else in
the row means what it claims.** Run this before trusting any number
here.

## CLI surface

| flag                  | meaning                                                                          |
| --------------------- | -------------------------------------------------------------------------------- |
| `--addresses=A..B`    | Geometric sweep, default `k=1` (same as log₂). Default `1..256`.                 |
| `--addresses=A..B@k`  | Geometric sweep with `k` samples per octave.                                     |
| `--stride_bytes=A..B` | Log₂ sweep, default `8..128`. Byte distance between consecutive slots.           |
| `--base_reg=0,1,2,3`  | Addressing form for store/load. See the kernel diagram. Default sweeps all four. |
| `--alu_ops=N`         | See above. Default `1`.                                                          |

See [`../cli.md`](../cli.md) for global flags.

## Reading the curves

Subtract `alu_ops` from `cycles_per_site` to get the raw forward
latency.

| Observation                          | Reading                                                                                                                                                                                                  |
| ------------------------------------ | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| Latency well under an L1D hit        | The load isn't reaching the cache — forwarding is being resolved early.                                                                                                                                  |
| Latency at the L1D hit cost or above | Conventional store-queue forwarding, no early resolution.                                                                                                                                                |
| Curve moves with `stride_bytes`      | Handling is line- or page-granular rather than per-address.                                                                                                                                              |
| `base_reg=0` below `base_reg=1`      | Stack-specific handling; SP-relative addressing is a special case.                                                                                                                                       |
| `base_reg=0` and `1` coincide        | Whatever is happening is not keyed to the stack pointer.                                                                                                                                                 |
| `base_reg=2,3` far above `0` and `1` | The match is on the register NAME, not the address. Forms 2 and 3 reach identical bytes through a register that provably holds SP, so a gap here means the comparison happens before any address exists. |

## Caveats

- **The chain must never load a repeating value.** A per-PC last-value
  load predictor will sever the chain and the row will report issue
  throughput as a latency -- roughly 0.4 cycles per link instead of the
  real number. The `alu_ops` carry increment is what keeps every load
  result fresh; see the dependent-chain section of
  [`../writing-a-benchmark.md`](../writing-a-benchmark.md) and validate
  with `scripts/chain_slope.py`.
- **Neither axis probes capacity.** The chain is serial, so exactly one
  store/load pair is ever in flight — there is nothing to exhaust.
  Sweeping `addresses` changes _which_ slot a link uses, not how many
  pairs are tracked at once, so a flat curve here is not evidence of a
  large tracking structure. A capacity probe needs concurrently live
  pairs, i.e. independent interleaved chains with one register each,
  which is a different kernel.
- **What `addresses` does establish**: that the behaviour survives an
  arbitrary rotating set of slots rather than only a repeat of the
  immediately preceding store, and — because the whole default sweep is
  L1D-resident — that no feature of the curve is a cache-capacity
  effect.
- **Addressing window.** `(addresses − 1) × stride_bytes` must not
  exceed 32744. AArch64 encodes an 8-byte-aligned `LDR`/`STR` immediate
  offset in 12 bits scaled by 8 (reach 32760), and SP-relative offsets
  additionally carry `SLJIT_LOCALS_OFFSET_BASE` (16 bytes). Points past
  the window are rejected rather than silently measured with an extra
  instruction per link. The bound is applied on x86_64 too, where
  `disp32` imposes no comparable limit, so both architectures sweep an
  identical parameter space.
- **The layout check is AArch64-only.** x86_64 `LDR`/`STR` displacement
  width varies with the offset (`disp8` vs `disp32`), so a uniform
  per-link byte stride does not hold there by construction. The kernel
  still emits and runs; it just doesn't carry the guarantee.
- **A rejected point aborts the whole sweep**, it is not skipped. Keep
  CLI overrides inside the addressing window.
- **Apple Silicon pinning.** See the project README's discipline
  section — probe and benchmark land on _some_ P-core, not necessarily
  the same one.

## Related docs

- Construction rationale: [`writing-a-benchmark.md`](../writing-a-benchmark.md).
- Project two-step workflow: [project README](../../README.md).

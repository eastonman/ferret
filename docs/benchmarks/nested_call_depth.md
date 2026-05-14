# `nested_call_depth` — how to run

Probes the depth of the CPU's **Return Address Stack (RAS)** by emitting a
chain of N nested `call`/`ret` pairs. Per-call cost is flat while
N ≤ RAS capacity; once N exceeds it, the oldest return addresses are
evicted, those rets miss RAS, and per-call cost steps up to a higher
plateau. The cliff position is the RAS capacity.

## Kernel structure

Variant 1 (default) is the canonical picture; variants 0 and 2
differ in fan-out per body. All three emit `depth + 1` functions
(`chain_main` + `BODY_1`…`BODY_depth`), with `chain_main` running
the outer loop and calling `BODY_depth`. Each `BODY_d` calls
`BODY_{d-1}` through one or more dispatch-selected sites. `BODY_0`
is a bare RET.

```
 chain_main:                    BODY_d  (variant 1, 1 ≤ d ≤ depth):
 ┌─────────────────────────┐    ┌──────────────────────────┐
 │ MOV  S0, iters          │    │ AND  S0, 1               │
 │ loop_top:               │    │ JZ   site_b              │
 │   CALL BODY_depth       │    │ site_a: CALL BODY_{d-1}  │ ──┐
 │   SUB  S0,S0,1; JNZ ↩  │    │ JMP  done                │   │
 │ RET                     │    │ site_b: CALL BODY_{d-1}  │ ──┤
 └─────────────────────────┘    │ done: RET                │   │
                                └──────────────────────────┘   │
   S0 = iteration counter,                                     │
   threaded through the chain                                  │
   in a callee-saved register                              ◀───┘
                                                          (next deeper body
                                                           returns through here)
 BODY_0:
 ┌─────────────────────────┐
 │ RET                     │  ← deepest level; pops the last return addr
 └─────────────────────────┘
```

Per-body fan-out is what `--variant` selects:

- **`--variant=0`** — one CALL site per body, no dispatch. Direct
  RET, single target per RET PC.
- **`--variant=1`** — two CALL sites per body, one CB dispatching
  on bit 0 of `S0` (perfectly predicted alternation).
- **`--variant=2`** — eight CALL sites per body, three CBs forming
  a binary tree dispatching on bits of a byte loaded from
  `path_table[row][i]`, where `row = S0 & (path_table_rows − 1)`
  rotates per outer iteration.

## The three kernel variants

Selectable via `--variant`. All three emit the same depth-N nested
chain; what changes is the dispatch each body uses to pick the call
site that fires this invocation.

### `--variant=0` — single call site (control / baseline)

Each body has **exactly one** CALL to the next body. Every CALL has a
fixed direct target; every RET pops a return address that the
BTB-indirect predictor has effectively just one possible value for per
ret PC. **No RAS-forcing**: a core with a strong indirect fallback for
`ret` can sometimes predict every return even when RAS overflows.

**Use it as a baseline.** Comparing the variant 0 curve to variants
1 and 2 tells you whether a cliff is genuinely from RAS overflow or
from some other artifact (I-cache, ITLB, BTB-direct capacity). If
variant 0 and variant 1 cliff at the same depth, you're seeing the
RAS. If variant 0 stays flat while variant 1 cliffs, your CPU has a
fallback indirect predictor strong enough to predict variant 0's
single-target rets.

Derived from ChipsAndCheese / Microbenchmarks `ReturnStackTest`
variant 0 ([source][cnc]); C-codegen port at [jiegec][jiegec].

### `--variant=1` — K=2 dispatch on bit 0 of the loop counter (default)

Each body has **two** CALL sites to the next body and **one**
conditional branch (`AND ...,1 ; JZ`) that picks one of them based on
bit 0 of the outer-loop counter (threaded through the chain in a
callee-saved register). Bit 0 alternates `0,1,0,1,…` per iteration, so
the dispatch branch is **perfectly predicted** after the first
iteration. Each ret PC has two possible correct targets across
iterations, defeating a simple last-target-per-PC indirect predictor.

Floor on Apple Silicon ≈ 2.7 cycles/pair — essentially the pure
hardware CALL + RET cost. **This is the canonical RAS-depth pattern**
and the default. It produces a clean cliff on most shipping cores.

Caveat: a deep TAGE-style indirect predictor with global history can
in principle learn the period-2 alternation. If you see no cliff with
variant 1 on a sophisticated core, try variant 2.

Derived from ChipsAndCheese / Microbenchmarks `ReturnStackTest`
variant 1 ([source][cnc]); C-codegen port at [jiegec][jiegec].

### `--variant=2` — K=8 dispatch from a per-iteration path table

Each body has **eight** CALL sites and a **three-CB binary tree** that
selects one of them based on a byte loaded from `path_table[row][i]`,
where `row` rotates per outer-loop iteration. Each ret PC has eight
possible correct targets across iterations, drawn from an 8-bit
pattern table of 256 rows by default. Even history-rich indirect
predictors cannot memorize the full table.

Floor ≈ 7-9 cycles/pair — higher than the other variants because of
the byte load + 3 dependent conditional branches. **The cliff position
is the same**; only the baseline shifts. Use this variant when you
suspect (or have measured) that variant 1's period-2 alternation is
being learned by the host's indirect predictor and masking the cliff.

This is the original ferret design — see
[the kernel-pattern note](../superpowers/specs/2026-05-12-ras-depth-kernel-pattern.md)
and [the full design](../superpowers/specs/2026-05-12-nested-call-depth-design.md)
for the rationale.

[cnc]: https://github.com/ChipsandCheese/Microbenchmarks/blob/master/AsmGen/tests/ReturnStackTest.cs
[jiegec]: https://github.com/jiegec/cpu-micro-benchmarks/blob/master/src/ras_size_gen.cpp

## CLI surface

Global flags (`--core`, `--freq`, `--reps`, `--warmup`, `--out`,
`--log-level`, `--seed`) are documented in [`../cli.md`](../cli.md);
the table below lists only the axes and options specific to this
benchmark.

| flag                  | meaning                                                  |
| --------------------- | -------------------------------------------------------- |
| `--depth=A..B`        | Sweep nesting depth from A to B inclusive, step 1.       |
| `--depth=v1,v2,…`     | Sweep an explicit list of depths.                        |
| `--variant=0\|1\|2`   | Kernel construction (default 1). See the variants section above. |
| `--path_table_rows=N` | Used **only by variant 2**. Per-iteration dispatch table row count, default 256, must be a power of two ≥ 2. Bigger ⇒ longer dispatch-pattern period (harder for indirect predictors to learn) but bigger memory footprint (`N × (depth+1)` bytes); once the table exceeds L1D, per-call cost inflates progressively with depth and the pre-cliff curve becomes a measurement of cache pressure rather than RAS pressure. The default keeps the table at ≤ 16 KB through depth 64 so it stays in L1D on every shipping core. |
| `--seed=…`            | Seeds the variant-2 path-table PRNG; combined with `depth` via `std::seed_seq` so distinct sweep points get distinct dispatch streams. Ignored by variants 0 and 1. |

## Reading the curves

Below the RAS cliff: per-call cost ≈ a few cycles (CALL + RET, plus the
variant's dispatch overhead). Above the cliff: per-call cost steps up
to a higher plateau as rets either mispredict or stall.

Real example, Apple Silicon P-core, `--depth=1..64`, all three
variants on the same core:

```
depth   variant 0 (none)  variant 1 (K=2)  variant 2 (K=8 table)
   1          4.0               2.9                7.3
   4          3.5               2.8                7.4
   8          2.9               2.7                7.5
  16          2.3               2.7                8.0
  32          2.2               2.8                8.2
  48          2.0               2.8                9.1
  58          2.1               2.7               10.9
  60         21.0              21.7               27.7    ← cliff
  64         20.0              20.6               28.7
```

All three variants cliff at the same depth (60), confirming the RAS
capacity. Variant 1 is essentially the ideal CALL + RET cost (2.7
cycles per pair) with the K=2 dispatch CB perfectly predicted. Variant
0 is slightly lower at high depths because its bodies have no AND/CB
at all, just the CALL. Variant 2's higher floor is the 3-CB dispatch
tree plus the path-table load — visible but doesn't move the cliff.

Other shipping cores typically show the cliff at the documented RAS
capacity (Cortex-A78 ≈ 24, Sunny/Golden Cove ≈ 16–32, Zen4 ≈ 32). On
cores with a strong history-rich indirect predictor, variant 1 may
not cliff because the predictor learns the period-2 alternation — use
variant 2 there.

> ⚠️ **Variant 2 path-table cache pressure.** The previous default of
> `path_table_rows=4096` produces a ~260 KB table at depth 64, which
> overflows L1D and inflates pre-cliff per-call cost progressively with
> depth. The current default of 256 keeps the table at ≤ 16 KB. If you
> raise `path_table_rows` to stress a host with a deep indirect
> predictor, keep `rows × (depth+1)` under L1D or the curve becomes a
> cache-pressure measurement instead.

## Caveats specific to this benchmark

- **Frequency scaling.** Probe and benchmark must share the same governor
  state. See the project README's discipline section.
- **macOS / Apple Silicon pinning.** macOS rejects per-core pin requests;
  ferret prints a warning and falls back to a P-cluster QoS hint. Probe
  and benchmark land on *some* P-core but not necessarily the same one,
  so cycle counts are stable per-cluster, not per-core. Use
  `taskpolicy -b` or `sudo nice` if you need stronger guarantees.
- **Predictor sophistication.** The cliff height depends on how weak the
  CPU's fallback indirect predictor for `ret` is once RAS misses.
  Variant 0 makes every ret PC have a single correct target, so on a
  core with a real BTB-indirect fallback the cliff for variant 0 could
  be smaller or absent — compare variant 0 and variant 1 to detect this
  empirically. Variant 1's K=2 alternation is period-2 and learnable by
  TAGE-style predictors with ≥ 1-bit history; variant 2's K=8 path
  table has an 8-bit period that's past every shipping predictor's
  global history.
- **Iteration budget vs. depth.** Per-call cost is reported per site;
  default `iterations()` aims for roughly 10 ms of work per measurement
  irrespective of depth.

## Related docs

- Construction rationale: `docs/superpowers/specs/2026-05-12-ras-depth-kernel-pattern.md`
- Full benchmark design: `docs/superpowers/specs/2026-05-12-nested-call-depth-design.md`
- Implementation plan: `docs/superpowers/plans/2026-05-12-nested-call-depth.md`
- Reference implementations for variants 0/1:
  - [ChipsAndCheese/Microbenchmarks `ReturnStackTest`][cnc]
  - [jiegec/cpu-micro-benchmarks `ras_size_gen.cpp`][jiegec]

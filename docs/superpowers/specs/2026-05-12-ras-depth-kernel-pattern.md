# RAS-Depth Microbenchmark — Kernel Construction Pattern

Date: 2026-05-12
Status: brainstorming fragment. Captures the kernel shape and the path-source
choice for the upcoming RAS-depth ferret benchmark. The full benchmark spec
(naming, sweep axes, CSV, testing) is still pending and will live in a
separate `*-design.md` once brainstorming completes.

## Goal

Reveal the depth of the CPU's Return Address Stack (RAS) by emitting a chain
of N nested calls and sweeping N. Per-call cost should be flat while
N ≤ RAS capacity and step up once N exceeds it.

The cliff at N = RAS capacity is only visible if `ret` mispredicts when RAS
overflows — i.e. if the fallback indirect predictor on `ret` **cannot
memorize the correct return target**. With a deterministic linear call chain
(unique callee per slot, single ret target per ret PC), even a simple
last-target-per-PC indirect predictor would memorize each target after
warmup and hide the cliff. The construction below denies that predictor a
single target to memorize.

## Static call graph: shared bodies, K = 2 call sites per body

One body per nesting level. Each body has **K = 2 distinct call sites**,
both targeting the same next-level body. The two sites are at distinct PCs
(`cs_a`, `cs_b`), so the instructions immediately after them — `rs_a`,
`rs_b` — are two distinct return-target PCs for the callee's single `ret`.

```text
BODY_i:                          ; one body per nesting level i ∈ [1, N]
    test  path_reg, 1
    jz    .site_b
.site_a:
    shr   path_reg, 1
    call  BODY_{i-1}             ; pushes rs_a
.rs_a:
    jmp   .done
.site_b:
    shr   path_reg, 1
    call  BODY_{i-1}             ; pushes rs_b
.rs_b:
.done:
    ret                          ; single ret PC, two possible targets

BODY_0:                          ; leaf
    ret
```

Properties:

- Every body's single `ret` PC has **two correct return targets** — `rs_a`
  and `rs_b` of whichever body called it. RAS predicts both perfectly
  (it stored the actual return address per call). A last-target-per-PC
  indirect predictor can memorize at most one, so when RAS misses, it
  mispredicts roughly half the time on these rets.
- `chain_main` (the outer driver invoked per outer-loop iteration) mirrors
  the same two-site dispatch into `BODY_N`, so the outermost ret — the
  one whose RAS push is evicted **first** when capacity is exceeded — is
  also multi-target.
- Each chain pass executes exactly **N + 1 call/ret pairs**. Runtime is
  linear in N. (Contrast with a static K-ary tree of bodies, which would
  execute K^N calls per pass.)

## Path-bit source: outer-loop counter (option i)

`path_reg` is loaded from the outer-loop iteration counter at the top of
each iteration. Each body tests the low bit and shifts right, so body *i*
samples a single bit of the counter and the bit position depends on the
body's depth from the top of the chain:

| body                                       | bit consumed of counter |
| ------------------------------------------ | ----------------------- |
| `chain_main`                               | bit 0                   |
| `BODY_N`                                   | bit 1                   |
| `BODY_{N-1}`                               | bit 2                   |
| …                                          | …                       |
| `BODY_1`                                   | bit N                   |

Bit *b* of a counter flips every 2^b iterations. The dispatch for each body
therefore samples a deterministic, periodic bit stream, and that stream
drives which of `{rs_a, rs_b}` becomes the correct return target for the
ret of the body it called.

### Why the dispatch branch is cheap

The conditional branch inside each body tests one bit of a counter. Bit 0
alternates every iteration (period 2); higher bits emit long runs (2^b
zeros followed by 2^b ones). Both are trivially predictable patterns, so
the dispatch CB does not mispredict at steady state and the baseline
per-call cost stays flat across N. No PRNG is needed for the dispatch path
to be cheap.

## Known limit: deep N starves the slow-cycling bits

The "two targets per ret" property only holds **if the relevant dispatch
bit actually flips during the measurement window**. Bit *b* flips every
2^b iterations, so the bits driving the outermost rets — which are the
ones that miss RAS first when N exceeds capacity — cycle slowly.

- For **shallow N** (≤ ~20) and ferret's default per-point iteration
  budget (~10⁵–10⁶ kernel calls), every dispatch bit flips many times.
  Both targets fire often, the indirect predictor sees a constantly
  alternating target, and the cliff is fully visible.
- For **deep N** (≥ ~24), bits beyond ~log2(budget) never flip during
  the window. The rets they drive see a constant target across the
  measurement and a simple indirect predictor memorizes it — those rets
  stop contributing to the cliff signal.

This is a known limitation of option (i). Two mitigations exist if we
need to probe deeper RAS than option (i) can resolve cleanly:

- **(ii) LFSR / xorshift path source.** A few JIT'd ops at the top of
  each outer iteration give every bit unpredictable variation regardless
  of iteration count. Cost: the dispatch CB inside each body now sees
  ~50 % mispredicts and the baseline rises by ~5–10 cycles per level.
- **(iii) Pre-seeded path table.** One memory load per iteration from a
  JIT-time-randomized array. Adds a load to the outer loop, gives full
  control over the bit stream.

Both fallbacks are deferred until measured option-(i) data shows the
cliff is being masked at depths we actually care about. Most shipping
cores have RAS in the 16–32 range, which option (i) covers.

## What this construction does NOT isolate

- I-cache / ITLB pressure from N + 1 distinct body PCs.
- BTB-direct capacity for the 2(N + 1) distinct call-site PCs (all
  targeting only N + 1 distinct callees, but tagged by N + 1 distinct
  caller PCs).

Both confounders are small at the sweep depths of interest (N ≤ ~64;
total static code on the order of a few KB), but they are noted here for
completeness. A body-spacing axis analogous to
`direct_branch_footprint`'s `spacing_bytes` can be added later if these
become a confound at the deepest sweep points.

## Open items for the full design doc

- Sweep range and step policy for N (default range, log2 vs. linear).
- Whether to expose `path_source = counter | lfsr | table` as a
  benchmark option from day one or wait until option (i) shows artifacts.
- Naming, CSV columns, and tests — picked up in the full
  `*-design.md`.

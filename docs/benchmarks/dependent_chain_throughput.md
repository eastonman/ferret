# `dependent_chain_throughput` — frequency-probe baseline

A back-to-back dependent ADD chain. Every ADD reads and writes the
same register, so it serializes at one ADD latency per op — exactly
one cycle on every common high-perf out-of-order core and on
in-order ARM Cortex-A class cores.

This benchmark exists to **probe the running core frequency** before
running another benchmark on the same core. The runner reports
ns/op; divide cycle count by 1 ns to get GHz. See the project README
for the two-step cycle workflow.

## Kernel structure

```text
 (chain_main)
  MOV R0, 1
  MOV R1, full_blocks        ─── outer-loop counter
  loop_top:
    ┌──────────────┐
    │ ADD R0,R0,1  │  ┐
    │ ADD R0,R0,1  │  │
    │ ADD R0,R0,1  │  │  UNROLL = 1024 dependent ADDs
    │   ...        │  │  (RAW on R0 between every pair)
    │ ADD R0,R0,1  │  ┘
    └──────────────┘
    SUB R1,R1,1    ─── full_blocks-times back-edge
    JNZ loop_top
  ── straight-line tail ──
    ADD R0,R0,1    ┐
    ADD R0,R0,1    │  chain_length % UNROLL ADDs
       ...         │
    ADD R0,R0,1    ┘
  RET
```

Annotated:

- All ADDs target `R0` — single live register, RAW dependency between
  every pair, so the chain runs at ADD latency.
- The inner loop body is **`UNROLL = 1024` ADDs** (an
  `emit_kernel`-level constant in `benchmarks/dependent_chain_throughput.cpp`).
  The outer loop runs `chain_length / UNROLL` times.
- A straight-line tail of `chain_length % UNROLL` ADDs makes the total
  op count match `chain_length` exactly. For `chain_length < UNROLL`
  only the tail runs.
- `sites_per_kernel = chain_length` and `iterations = 1`, so the
  runner reports ns per ADD = ns per cycle on a 1-IPC core.

## CLI surface

| flag               | meaning                                                         |
| ------------------ | --------------------------------------------------------------- |
| `--chain_length=N` | Total ADD count emitted into the kernel. Default `100_000_000`. |

See [`../cli.md`](../cli.md) for global flags (`--core`, `--freq`,
`--reps`, `--warmup`, `--out`, etc.).

The benchmark has no per-bench options.

## Reading the curves

Single-row output: one ns/op number. Divide 1 ns by that number to
get the running frequency in GHz. `scripts/freq.py` does this for
you:

```sh
build/ferret run dependent_chain_throughput --core=3 --out=/tmp/freq.csv
python3 scripts/freq.py /tmp/freq.csv
# → estimated_freq=4.521GHz
```

The reported frequency holds for the duration of the run on the
pinned core, modulo the caveats in the project README's discipline
section (frequency scaling, heterogeneous cores, Apple Silicon
pinning).

## Caveats

- **1-IPC assumption.** The whole probe rests on dependent ADD
  latency = 1 cycle. This holds on every shipping x86_64 and arm64
  core ferret targets. If you port ferret to an unusual architecture,
  validate this assumption first.
- **Single-threaded.** The probe pins to one core; the reported
  frequency is whatever that core was running at during the
  measurement, which may differ from other cores on heterogeneous
  CPUs.

## Related docs

- Construction rationale: [`writing-a-benchmark.md`](../writing-a-benchmark.md) (worked example A).
- Project two-step workflow: [project README](../../README.md).

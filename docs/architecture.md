# Ferret Architecture

Ferret JIT-emits microbenchmarks at runtime, times them with
free-running tick counters, sweeps a parameter axis, and writes one CSV
row per parameter point. This document is the live map of the codebase;
the per-feature specs under `superpowers/specs/` are point-in-time
records and may be out of date.

## End-to-end flow

```
main.cpp (CLI11 wiring)
  → ferret::list_command()        → stdout
  → ferret::run(RunOptions)
      → BenchmarkRegistry::create  // factory lookup
      → classify_overrides         // axes vs options
      → sweep::expand              // cartesian product
      → measure_all
          ↳ JittedKernel           // sljit emit + codegen
          ↳ runner::measure        // warmup + reps timed
      → emit_csv via CsvWriter
```

## Modules

Every public header lives under `include/ferret/`. The list is
alphabetical; each line is one sentence on responsibility.

- `axis.hpp` — declarative parameter axis (Range / Log2Range /
  GeomRange / Values) used by benchmarks to describe what to sweep.
- `bench_helpers.hpp` — JIT-time helpers for benchmark TUs:
  `emit_outer_loop`, `compute_iterations`, `verify_uniform_spacing`.
  Pulls in `sljitLir.h`; not for use from non-JIT public headers.
- `benchmark.hpp` — base class benchmark authors subclass, plus the
  registry and registration macro.
- `cli_axis.hpp` — parses CLI values like `1..32768`, `16,32,64`, or
  `--option=42` into axis or option values.
- `csv.hpp` — writes one CSV row per measurement to a caller-owned
  ostream.
- `freq.hpp` — parses `--freq=4.521GHz` and similar suffixed numbers
  into hertz.
- `jit.hpp` — RAII handle for an sljit-compiled kernel; calls
  `verify_layout` post-codegen.
- `log.hpp` — spdlog-backed logger aliased as `flog::` at every use
  site (avoids the `::log(double)` collision from `<math.h>`).
- `padding.hpp` — emits architecture-correct NOPs to pad branch sites.
- `params.hpp` — insertion-ordered key/int64 map carrying one parameter
  point through the runner and into the CSV.
- `parse.hpp` — `parse_int()`: shared low-level integer parser used by
  `cli_axis.cpp` and other consumers.
- `permute.hpp` — Sattolo's algorithm, a single Hamiltonian cycle over
  `{0..n-1}`, used by benchmarks that want to defeat sequential
  prefetch.
- `pinning.hpp` — best-effort core pin, priority boost, and memory lock.
- `run_command.hpp` — `ferret::run(RunOptions)` and `list_command()`,
  the two CLI entry points exposed by `main.cpp`.
- `runner.hpp` — `runner::measure` runs warmup + N timed reps and
  returns ticks_min, ticks_median.
- `sweep.hpp` — cross-product over each axis's expanded values, with
  optional per-axis overrides.
- `timing.hpp` — per-arch `arch_now_ticks()` (rdtscp on x86_64,
  cntvct_el0 on aarch64) and a lazily-calibrated `ticks_per_ns()`.

## Source layout

`src/` is mostly a flat mirror of `include/ferret/` (one `.cpp` per
header). Three subdirectories carry arch- or OS-conditional code that
`CMakeLists.txt` selects at configure time:

- `src/timing/` — `x86_64.cpp` (`rdtscp`) **or** `aarch64.cpp`
  (`cntvct_el0`), plus the shared `calibrate.cpp`. AArch64 reads the
  exact frequency from `cntfrq_el0`; x86_64 calibrates against a short
  wall-clock sleep, so its `ticks_per_ns()` is approximate (±1%
  depending on scheduler jitter).
- `src/pinning/` — `posix_common.cpp` for `boost_priority` /
  `lock_memory`, plus `linux.cpp` (`pthread_setaffinity_np`) **or**
  `macos.cpp` (`thread_policy_set` with a P-cluster QoS fallback —
  Apple Silicon does not implement per-core affinity; see the README's
  discipline section).
- `src/padding/` — `x86_64.cpp` **or** `aarch64.cpp`, each emitting the
  arch-correct NOP encoding for `emit_nops`.

`benchmarks/` holds one `.cpp` per benchmark, compiled into the
`ferret_benchmarks` OBJECT library (see `writing-a-benchmark.md`).

## Adding a benchmark

See `writing-a-benchmark.md`. The four existing benchmarks under
`benchmarks/` are worked examples of the frequency-probe and
parameter-sweep patterns.

## Usage

See `../README.md` for the command-line interface and the two-step
cycle workflow (probe frequency, then run the actual benchmark on the
same core).

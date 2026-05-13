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

- `axis.hpp` — declarative parameter axis (Range / Log2Range / Values)
  used by benchmarks to describe what to sweep.
- `benchmark.hpp` — base class benchmark authors subclass, plus the
  registry and registration macro.
- `cli_axis.hpp` — parses CLI values like `1..32768`, `16,32,64`, or
  `--option=42` into axis or option values.
- `csv.hpp` — writes one CSV row per measurement to a caller-owned
  ostream.
- `freq.hpp` — parses `--freq=4.521GHz` and similar suffixed numbers
  into hertz.
- `jit.hpp` — RAII handle for an sljit-compiled kernel.
- `log.hpp` — spdlog-backed logger aliased as `flog::` at every use
  site (avoids the `::log(double)` collision from `<math.h>`).
- `padding.hpp` — emits architecture-correct NOPs to pad branch sites.
- `params.hpp` — insertion-ordered key/int64 map carrying one parameter
  point through the runner and into the CSV.
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

## Adding a benchmark

See `writing-a-benchmark.md`. The two existing benchmarks under
`benchmarks/` are worked examples of the frequency-probe and
parameter-sweep patterns.

## Usage

See `../README.md` for the command-line interface and the two-step
cycle workflow (probe frequency, then run the actual benchmark on the
same core).

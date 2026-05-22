# Ferret

**F**ront**E**nd **R**eve**R**se-**E**ngineering **T**oolkit — a JIT-driven cross-platform
microbenchmark framework for probing CPU frontend microarchitectural
structures (BTB, RAS, BPU, decoded-uop cache, ITLB, …).

Ferret emits parameterized microbenchmarks at runtime, measures their
per-site cost via free-running timing counters, sweeps a parameter axis,
and writes one CSV row per parameter point. A Python script plots the
resulting curves so you can spot capacity/associativity cliffs.

## Supported platforms

| Arch    | Linux | macOS | Android |
| ------- | :---: | :---: | :-----: |
| x86_64  |   ✓   |   —   |    ✓    |
| AArch64 |   ✓   |   ✓   |    ✓    |

RISC-V and LoongArch are reachable through sljit but not yet supported.

## Quickstart

```sh
nix develop
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

Full build options, sanitizer matrix, and non-Nix recipes: [`docs/build.md`](docs/build.md).

## The two-step cycle workflow

Ferret reports per-site cost in **CPU cycles** when you supply the
running core frequency, in **nanoseconds** otherwise. Cycles are the
preferred unit because the absolute number is meaningful information
about the structure under test. Ferret never auto-probes the
frequency — it asks you to do it explicitly.

```sh
# Step 1: probe the running frequency on core 3.
build/ferret run dependent_chain_throughput --core=3 --out=/tmp/freq.csv
python3 scripts/freq.py /tmp/freq.csv
# → estimated_freq=4.521GHz

# Step 2: run the actual benchmark with --freq, pinned to the same core.
build/ferret run direct_branch_footprint --core=3 \
    --branches=1..32768 --spacing_bytes=16..128 \
    --freq=4.521GHz --out=/tmp/btb.csv

# Step 3: line plot. Default --out=*.png writes a static image
# (requires Chrome for kaleido; the Nix dev shell provides it on Linux).
python3 scripts/plot.py line /tmp/btb.csv --out=/tmp/btb.png

# Or write interactive HTML (rotate, zoom, hover for exact values):
python3 scripts/plot.py line /tmp/btb.csv --out=/tmp/btb.html

# Or with spacing_bytes on X (branches becomes the legend):
python3 scripts/plot.py line /tmp/btb.csv --x=spacing_bytes --out=/tmp/btb-by-spacing.html

# Or as a 2D heatmap (branches × spacing_bytes, cycles per site as color):
python3 scripts/plot.py heatmap /tmp/btb.csv --out=/tmp/btb-heatmap.html
```

Static image formats (`.png` / `.svg` / `.pdf` / `.jpg` / `.webp`)
require Chrome or Chromium on PATH so kaleido can run a headless
export. The Nix dev shell ships `pkgs.chromium` on Linux; on macOS
or non-Nix systems install Chrome via your package manager or run
`python -m plotly.io._kaleido install_chrome`. HTML output has no
such requirement and is the default when `--out` is omitted (a temp
file opens in the system browser).

CLI flags and axis syntax: [`docs/cli.md`](docs/cli.md).

## Benchmarks

| Name                                                                             | Targets                                  |
| -------------------------------------------------------------------------------- | ---------------------------------------- |
| [`dependent_chain_throughput`](docs/benchmarks/dependent_chain_throughput.md)    | running core frequency / 1-IPC baseline  |
| [`direct_branch_footprint`](docs/benchmarks/direct_branch_footprint.md)          | direct-jump BTB capacity                 |
| [`nested_call_depth`](docs/benchmarks/nested_call_depth.md)                      | Return Address Stack (RAS) depth         |
| [`branch_history_footprint`](docs/benchmarks/branch_history_footprint.md)        | conditional-branch direction-predictor capacity |

Each benchmark page has the kernel structure, CLI surface, and reading-the-curves guide.

```sh
build/ferret list   # registered benchmark names
```

## Discipline (a.k.a. caveats)

Ferret does what user-space can do to make timing reliable: pins a
core, raises priority, mlocks memory, runs warmup iterations, takes
the **min** of K repetitions per data point. Everything else is your
responsibility:

- **Frequency scaling.** Ferret cannot pin core frequency without root.
  Run with a fixed-frequency governor (Linux: `cpupower frequency-set
-g performance`) or document that boost was active.
- **Heterogeneous cores** (Apple P/E, ARM big.LITTLE, Android). Pin
  with `--core=` so probe and target benchmark execute on the _same_
  core. Different cores can have different microarchitectures and
  different running frequencies.
- **Apple Silicon pinning.** macOS on arm64 (M-series) does not
  implement per-core thread affinity — `thread_policy_set` returns
  `KERN_NOT_SUPPORTED` for every core number. On that platform ferret
  falls back to a `QOS_CLASS_USER_INTERACTIVE` hint that strongly
  prefers the P-cluster, prints a warning, and treats `--core=N` as
  informational. Probe and benchmark land on _some_ P-core, not
  necessarily the same one, so cycle counts are stable per-cluster but
  not per-core. Run with `taskpolicy -b` or `sudo nice` if you need
  stronger guarantees.
- **Frequency-probe assumption.** `dependent_chain_throughput` assumes
  dependent ADD latency = 1 cycle. This holds on every common high-perf
  out-of-order core and on in-order ARM Cortex-A class cores. If you
  use ferret on an unusual architecture, validate the assumption first.
- **System noise.** App Nap (macOS), Doze (Android), background tasks,
  AV scanners, etc. Ferret does not detect or report on noise levels —
  it just runs. If your data looks suspicious, quiesce the box and
  rerun.

## Documentation

- [`AGENTS.md`](AGENTS.md) — operational checklist for contributors and agentic workers (pre-PR commands, conventions, CI gates, footguns).
- [`docs/architecture.md`](docs/architecture.md) — codebase overview and module map.
- [`docs/build.md`](docs/build.md) — full build, sanitizer matrix, single-test recipes.
- [`docs/cli.md`](docs/cli.md) — global CLI flags and axis syntax.
- [`docs/benchmarks/`](docs/benchmarks/) — per-benchmark kernel structure and options.
- [`docs/writing-a-benchmark.md`](docs/writing-a-benchmark.md) — guide for adding a new benchmark.
- [`docs/contributing.md`](docs/contributing.md) — formatting and linting (points to AGENTS.md).

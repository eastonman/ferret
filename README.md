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

RISC-V and LoongArch are reachable through sljit but not yet supported

## Build

### Dependency requirements

- C++20 compiler
- CMake > 3.20
- Ninja or GNU Make
- Python 3
- Git (for FetchContent and submodules)

### Nix (recommended)

```sh
nix develop
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

### Plain CMake

Make sure the dependency requirements above are installed.
CMake will FetchContent CLI11, GoogleTest, and sljit if they aren't on
the system search path:

```sh
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Use

### List benchmarks

```sh
build/ferret list
```

### The two-step cycle workflow

ferret reports per-site cost in **CPU cycles** when you supply the
running core frequency, in **nanoseconds** otherwise. Cycles are the
preferred unit because the absolute number is meaningful information
about the structure under test. ferret never auto-probes the
frequency — it asks you to do it explicitly.

```sh
# Step 1: probe the running frequency on core 3.
build/ferret run dependent_chain_throughput --core=3 --out=/tmp/freq.csv
python3 scripts/freq.py /tmp/freq.csv
# → estimated_freq=4.521GHz

# Step 2: run the actual benchmark with --freq, pinned to the same core.
build/ferret run direct_branch_footprint --core=3 \
    --branches=1..32768 --spacing_bytes=64 \
    --freq=4.521GHz --out=/tmp/btb.csv

# Step 3: plot — picks cycles_per_site automatically because freq was set.
python3 scripts/plot.py /tmp/btb.csv --out=/tmp/btb.png
```

### CLI flags

```
ferret run <name> [options] [--<axis>=value-or-range]
  --out=PATH        CSV output (default stdout)
  --core=N          pin measurement thread to core N
  --freq=4.521GHz   running frequency, enables cycle columns
  --reps=K          repetitions per param point (default 7)
  --warmup=W        un-timed calls before measurement (default 1)
  --<axis>=v        explicit single axis value
  --<axis>=v1,v2    explicit value list
  --<axis>=lo..hi   range using the axis's declared step policy
```

## Formatting and linting

Formatters and linters run in CI and must pass before merging.

- C++: `clang-format` (style in `.clang-format`) and `clang-tidy` (checks
  in `.clang-tidy`).
- Python: `ruff format` and `ruff check` (config in `pyproject.toml`).

Apply formatters locally:

```sh
./scripts/format.sh
```

Verify the way CI does:

```sh
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
./scripts/lint.sh
```

All tools are provided by `nix develop`.

## Discipline (a.k.a. caveats)

ferret does what user-space can do to make timing reliable: pins a
core, raises priority, mlocks memory, runs warmup iterations, takes
the **min** of K repetitions per data point. Everything else is your
responsibility:

- **Frequency scaling.** ferret cannot pin core frequency without root.
  Run with a fixed-frequency governor (Linux: `cpupower frequency-set
-g performance`) or document that boost was active.
- **Heterogeneous cores** (Apple P/E, ARM big.LITTLE, Android). Pin
  with `--core=` so probe and target benchmark execute on the _same_
  core. Different cores can have different microarchitectures and
  different running frequencies.
- **Frequency-probe assumption.** `dependent_chain_throughput` assumes
  dependent ADD latency = 1 cycle. This holds on every common high-perf
  out-of-order core and on in-order ARM Cortex-A class cores. If you
  use ferret on an unusual architecture, validate the assumption first.
- **System noise.** App Nap (macOS), Doze (Android), background tasks,
  AV scanners, etc. ferret does not detect or report on noise levels —
  it just runs. If your data looks suspicious, quiesce the box and
  rerun.

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

### Sanitizer builds

Off by default — timing-sensitive runs use clean code. Enable via
`-DFERRET_SANITIZER=<mode>`:

| Mode                | Catches                                                      |
|---------------------|--------------------------------------------------------------|
| `address`           | use-after-free, heap/stack overflow, leaks (LSan, Linux)     |
| `undefined`         | signed overflow, null deref, alignment, type mismatches      |
| `address+undefined` | both of the above (default in CI)                            |
| `thread`            | data races, deadlocks                                        |

```sh
cmake -S . -B build-asan -GNinja -DFERRET_SANITIZER=address+undefined
cmake --build build-asan
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 \
  ctest --test-dir build-asan --output-on-failure
```

CI runs `address+undefined` and `thread` on Linux x86_64 and arm64
(`.github/workflows/sanitizers.yml`). macOS sanitizer support depends
on the toolchain; nixpkgs-clang ASan/TSan currently has runtime-init
issues on Apple Silicon — use UBSan there or build under a Linux
container.

## Documentation

- [`docs/architecture.md`](docs/architecture.md) — codebase overview and module map.
- [`docs/writing-a-benchmark.md`](docs/writing-a-benchmark.md) — guide for adding a new benchmark.

## Use

### List benchmarks

```sh
build/ferret list
```

### Benchmarks

Each benchmark probes one frontend structure. Use the table to pick the
one matching the question you're asking.

| Name                          | Targets                                  | Notes                                                                            |
|-------------------------------|------------------------------------------|----------------------------------------------------------------------------------|
| `dependent_chain_throughput`  | running core frequency / 1-IPC baseline  | dependent ADD chain                                                              |
| `direct_branch_footprint`     | direct-jump BTB capacity                 | N unconditional branches; `--sattolo_permute=1` defeats spatial I-cache prefetch |

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
ferret run <name> [options] [--<axis>=value-or-range] [--<benchmark-option>=v]
  --out=PATH        CSV output (default stdout)
  --core=N          pin measurement thread to core N
  --freq=4.521GHz   running frequency, enables cycle columns
  --reps=K          repetitions per param point (default 7)
  --warmup=W        un-timed calls before measurement (default 1)
  --log-level=L     trace|debug|info|warn|error|critical|off (default warn)
  --seed=S          RNG seed for benchmarks that randomize (default 1)
  --<axis>=v        explicit single axis value
  --<axis>=v1,v2    explicit value list
  --<axis>=lo..hi   range using the axis's declared step policy
  --<option>=v      scalar per-benchmark option override (not swept)
```

### Per-benchmark options

`direct_branch_footprint` accepts `--sattolo_permute=0|1`. The default
(`0`) wires each branch to fall through to the next in layout order.
`--sattolo_permute=1` rewires the jump targets as a uniform random
Hamiltonian cycle over the same N branches (Sattolo's algorithm, seeded
by `--seed` mixed with the branch count and spacing). N branches still
execute per outer-loop iteration, but the executed PC order is
unpredictable — useful for isolating the BTB contribution from
sequential-prefetch and I-cache spatial-locality effects.

`nested_call_depth` — N nested `call`/`ret` pairs at distinct PCs with
K = 8 shared-callee dispatch reads from a per-iteration path table.
Sweep `--depth=1..64` to reveal the cliff at the RAS capacity. See
[`docs/benchmarks/nested_call_depth.md`](docs/benchmarks/nested_call_depth.md)
for the full workflow, options, and example output.

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
  AV scanners, etc. ferret does not detect or report on noise levels —
  it just runs. If your data looks suspicious, quiesce the box and
  rerun.

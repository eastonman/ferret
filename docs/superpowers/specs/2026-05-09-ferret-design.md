# ferret v1 — Design

Date: 2026-05-09

## 1. Goal

ferret is a JIT-driven cross-platform microbenchmark framework for reverse-engineering CPU **frontend** microarchitectural structures (BTB, RAS, BPU, decoded-uop cache, fetch, ITLB, …).

It works by emitting parameterized microbenchmarks at runtime, measuring their per-site cost via free-running timing counters, and sweeping a parameter axis so the user can plot the resulting curve and identify capacity/associativity cliffs.

The primary signal is **per-site cycles vs. footprint**: as the workload's working set grows, per-site cost stays flat until it exceeds the structure under test, then steps to a higher plateau. The cliff position reveals the structure's capacity.

## 2. Scope

### In scope for v1

- Cross-architecture JIT codegen via **sljit**, building on **x86_64** and **AArch64** in v1.
- Cross-OS execution: **Linux**, **macOS** (Apple Silicon), and **Android** (userspace via Termux/NDK).
- Microbenchmark framework: static-registered C++ benchmarks, sweep-axis driver, CSV output.
- Measurement: timing-only, using free-running counters (`rdtsc`/`rdtscp` on x86, `CNTVCT_EL0` on ARM64).
- One v1 microbenchmark: **`direct_branch_footprint`**.
- Python plot script (`scripts/plot.py`) consuming ferret's CSV.
- Unit and integration tests via **GoogleTest** + CTest.
- Build via CMake. **Nix flake is the primary dependency-management path**; FetchContent is the fallback when Nix isn't available.
- CI on GitHub Actions for x86_64 Linux, ARM64 Linux, macOS Apple Silicon, plus a Nix job.

### Out of scope for v1

- PMU-event measurement (rejected: vendor heterogeneity makes events non-portable; timing is canonical).
- Additional microbenchmarks (RAS, BPU history, indirect branch, alignment sweeps, etc.) — added incrementally in later releases.
- RISC-V and LoongArch backends. sljit covers them; we don't enable or test them in v1.
- Android NDK cross-compile in CI (deferred to v1.1 once the tool has been used on a phone).
- Noise/stability detection, refusing-to-run on bad system state. ferret does what user-space can do and documents the rest as the user's responsibility.

## 3. Architecture

### 3.1 Module breakdown

```
ferret/
├── flake.nix                  # devShell + ferret package output
├── flake.lock                 # pinned inputs
├── nix/
│   ├── sljit.nix              # vendored sljit derivation (sljit not in nixpkgs)
│   └── ferret.nix             # ferret package derivation
├── CMakeLists.txt             # find_package → FetchContent fallback
├── include/ferret/
│   ├── benchmark.hpp          # Benchmark base class, FERRET_BENCHMARK macro
│   ├── sweep.hpp              # SweepAxes, parameter expansion
│   ├── timing.hpp             # arch_now_ticks(), tick→ns calibration
│   ├── pinning.hpp            # cross-OS core pinning, priority, mlock
│   └── runner.hpp             # measurement loop, min-of-K
├── src/
│   ├── main.cpp               # CLI11 entry, dispatches to runner
│   ├── benchmark_registry.cpp
│   ├── runner.cpp             # warmup, K repetitions, aggregation
│   ├── sweep.cpp              # axis expansion (range / log2 / value list)
│   ├── timing/
│   │   ├── x86_64.cpp         # rdtsc / rdtscp
│   │   └── aarch64.cpp        # CNTVCT_EL0 read
│   ├── pinning/
│   │   ├── linux.cpp          # sched_setaffinity + setpriority + mlockall
│   │   ├── macos.cpp          # thread_policy_set + setpriority
│   │   └── android.cpp        # thin shim over linux.cpp
│   └── output/
│       └── csv.cpp            # writes ticks_per_site rows
├── benchmarks/
│   └── direct_branch_footprint.cpp
├── scripts/
│   └── plot.py                # matplotlib cliff plotter
├── tests/
│   ├── CMakeLists.txt         # gtest_discover_tests, registers with CTest
│   ├── test_sweep.cpp
│   ├── test_timing.cpp
│   ├── test_runner.cpp
│   ├── test_csv.cpp
│   ├── test_benchmark_registry.cpp
│   └── test_integration.cpp   # end-to-end direct_branch_footprint smoke
└── .github/workflows/
    └── ci.yml
```

### 3.2 Module boundaries

Each module has one responsibility, a small public surface, and minimal internal coupling:

- **`timing`** — read a free-running counter as cheaply as possible. Public: `uint64_t arch_now_ticks()`, `double calibrate_ticks_per_ns()`. Selected at compile time by arch.
- **`pinning`** — set up the measurement thread. Public: `pin_to_core(int)`, `boost_priority()`, `lock_memory()`. Per-OS implementation behind one header. All operations are best-effort and log a warning to stderr on failure rather than aborting.
- **`sweep`** — given `SweepAxes`, produce an iterator of `Params` rows. No I/O, no codegen.
- **`runner`** — given a benchmark + Params, return a `MeasurementRow`. Drives warmup, K repetitions, takes the min.
- **`benchmark`** — abstract base class with `axes()`, `sites_per_kernel(p)`, `iterations(p)`, `emit_kernel(sljit_compiler*, p)`. Concrete benchmarks live under `benchmarks/`; each registers via `FERRET_BENCHMARK("name", Class)`.
- **`benchmark_registry`** — static registry populated at program start by `FERRET_BENCHMARK` macros. Lookup by name; missing name returns null and main produces an actionable error.
- **`output/csv`** — writes one row per param point, fixed schema.
- **`main`** — parses CLI flags via CLI11, dispatches to runner.

You can swap the timing primitive without touching the runner. You can add a benchmark without touching anything else. Each unit is small enough to test in isolation.

### 3.3 Data flow

When the user runs `ferret run direct_branch_footprint --branches=1..32768 --spacing=64 --out=btb.csv`:

```
main.cpp (CLI11 parse)
   │
   ▼
benchmark_registry::lookup("direct_branch_footprint")
   │
   ▼
sweep::expand(benchmark.axes(), cli_overrides) → vector<Params>
   │
   ▼
pinning::setup() (best-effort)
   │
   ▼
for each Params row:
   ┌────────────────────────────────────────────────────────┐
   │ sljit_compiler* c = sljit_create_compiler();           │
   │ benchmark.emit_kernel(c, p);                            │
   │ KernelFn fn = sljit_generate_code(c);                  │
   │ runner::measure(fn, K_reps, warmup):                   │
   │   fn();                              // warmup         │
   │   for k in 0..K:                                       │
   │     t0 = arch_now_ticks();                              │
   │     fn();                                               │
   │     t1 = arch_now_ticks();                              │
   │     samples[k] = t1 - t0;                               │
   │   return MeasurementRow {                              │
   │     ticks_min = min(samples),                          │
   │     ticks_med = median(samples),                       │
   │     iters     = benchmark.iterations(p),               │
   │     sites     = benchmark.sites_per_kernel(p),         │
   │     reps      = K                                      │
   │   };                                                   │
   │ csv_writer.write(p, measurement);                      │
   │ sljit_free_code(fn);                                   │
   └────────────────────────────────────────────────────────┘
   │
   ▼
csv flushed and closed, exit
```

The kernel function loops internally for `iters` cycles (the JIT-emitted code contains the outer `iters` loop, the inner per-site sequence is the N branches). One `fn()` call therefore executes `iters × sites` operations, amortizing `arch_now_ticks` overhead.

Per-site cost: `ticks_min / (iters × sites)`.

## 4. Tooling decisions

### 4.1 sljit (codegen)

**Chosen.** Single library covering x86_32/64, ARM 32/64, RISC-V 32/64, LoongArch64, PPC, MIPS, s390x. Actively maintained (BSD, used as the JIT engine inside PCRE2).

API style: pre-built IR (`sljit_emit_op2(c, SLJIT_ADD, dst, src1, src2)` etc.). Same ops on every architecture. The user does not design the IR.

**Rejected:** AsmJit (maintainer announced "DEVELOPMENT SUSPENDED" in January 2026, no new architecture ports). xbyak (x86-only). LLVM MC (heavy, API churn). Hand-rolled per-arch emitter (more upfront work, no portability win over sljit).

### 4.2 Measurement: timing-only

Free-running counters read from user mode:

| Arch / OS | Primitive |
|---|---|
| x86 (any OS) | `rdtsc` / `rdtscp` |
| ARM64 Linux/Android | `mrs CNTVCT_EL0` |
| ARM64 macOS | `CNTVCT_EL0` (or `mach_absolute_time`) |

These read free-running counters at fixed frequency. They yield wall ticks, not core cycles. For cliff detection this is sufficient: when the workload is frontend-bound, time tracks frontend cost linearly. The CSV column is named `ticks_*` (not `cycles_*`) and the plot axis is labelled "cycles per site (TSC ticks)" so the unit is honest.

PMU access was considered and **rejected** for v1: cross-vendor event-set heterogeneity breaks portability. May be added as an optional secondary signal in a later release.

### 4.3 System-state discipline: best-effort user-space only

ferret does:
- Pin the measurement thread to a core (`sched_setaffinity` / `pthread_setaffinity_np` / `thread_policy_set`).
- Boost thread priority (`setpriority`).
- `mlockall` (or `mlock` over the JIT region) to avoid paging.
- Warmup iterations before each measurement.
- Take the **min** of K repetitions per parameter point. Min is the right statistic here because timing noise can only slow the kernel down, never speed it up.

Concrete K and warmup-count values are implementation choices for the plan, with sensible defaults overridable from the CLI.

ferret does **not**:
- Detect noise levels or compute stability scores.
- Refuse to run based on system state.
- Touch frequency governors, SMT siblings, App Nap, Doze, or anything requiring root.

Frequency scaling, big.LITTLE migration on Android-without-root, and similar are documented as the user's responsibility.

### 4.4 Build & dependencies

**CMake** is the build tool. Two paths to dependencies:

**Primary: Nix flake.** `nix develop` opens a shell with cmake, ninja, clang, CLI11, GoogleTest, sljit, and a Python with matplotlib + pandas. `nix build` produces the ferret binary. The flake pins all inputs via `flake.lock`.

```nix
# flake.nix (sketch)
{
  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
    sljit-src = { url = "github:zherczeg/sljit"; flake = false; };
  };
  outputs = { self, nixpkgs, flake-utils, sljit-src }:
    flake-utils.lib.eachDefaultSystem (system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        sljit = pkgs.callPackage ./nix/sljit.nix { src = sljit-src; };
      in {
        devShells.default = pkgs.mkShell {
          packages = [
            pkgs.cmake pkgs.ninja pkgs.clang
            pkgs.cli11 pkgs.gtest sljit
            (pkgs.python3.withPackages (ps: [ ps.matplotlib ps.pandas ]))
          ];
        };
        packages.default = pkgs.callPackage ./nix/ferret.nix {
          inherit sljit; src = self;
        };
      });
}
```

sljit is not in nixpkgs, so it's vendored via a small derivation in `nix/sljit.nix` driven by the `sljit-src` flake input.

**Fallback: FetchContent.** When the user has no Nix, the same `cmake` invocation works. The CMakeLists uses `find_package` first, falls back to FetchContent if the package isn't found:

```cmake
find_package(CLI11 QUIET)
if(NOT CLI11_FOUND)
  include(FetchContent)
  FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2)
  FetchContent_MakeAvailable(CLI11)
endif()
# same pattern for GTest, sljit
```

This means the same source tree builds in three contexts without configuration: inside `nix develop` (uses nix-provided libs), inside `nix build` (uses derivation-supplied libs), or on a plain box with just CMake (FetchContent clones the deps).

**Rejected:** vcpkg (overkill for two small deps). Git submodules (footguns).

### 4.5 Test framework: GoogleTest

Locked in. CTest discovers tests via `gtest_discover_tests`. Available in nixpkgs (`pkgs.gtest`) and via FetchContent fallback.

### 4.6 CI

GitHub Actions, three native build/test jobs plus one Nix job:

```yaml
jobs:
  build-test:
    strategy:
      matrix:
        os: [ubuntu-latest, ubuntu-24.04-arm, macos-latest]
    steps:
      - checkout
      - cmake -S . -B build -GNinja
      - cmake --build build
      - ctest --test-dir build --output-on-failure

  nix:
    runs-on: ubuntu-latest
    steps:
      - checkout
      - DeterminateSystems/nix-installer-action
      - nix flake check
      - nix build
      - nix develop --command ctest --test-dir build --output-on-failure
```

Native jobs validate the FetchContent fallback path. The Nix job validates the flake. Both run unit and integration tests. CI does **not** assert on cliff positions — runners are noisy VMs and microarchitectural details vary across host hardware.

Android NDK cross-compile in CI is deferred to v1.1.

## 5. The v1 microbenchmark: `direct_branch_footprint`

### 5.1 Naming convention

Benchmarks are named by the **workload pattern** they emit, not by the microarchitectural structure they're assumed to reveal. The same kernel data can answer multiple questions depending on how it's analyzed (a direct-branch-footprint sweep can show BTB capacity, BTB hierarchy levels, indexing/associativity effects, and prefetcher behavior — each as a different slice of the same data). Baking one interpretation into the name misleads readers and discourages reuse.

Inferences ("this looks like a 4096-entry BTB cliff") happen in plot titles, README commentary, and analysis docs — never in benchmark identifiers.

### 5.2 What it measures

The kernel emits N unconditional direct branches, each at a distinct PC, all visited in a tight loop:

```
loop_top:
  jmp .L1
  <pad to spacing_bytes>
.L1:
  jmp .L2
  <pad>
  ...
.LN:
  jmp loop_top
```

Padding is multi-byte NOPs sized to reach the requested per-branch spacing. Each branch is unconditional and direct (always taken, single static target) so prediction policy doesn't confound the measurement — it isolates the effect of branch-PC → target lookup.

Per-site cost is flat as long as N branches fit in the BTB (or equivalent target-lookup structure). When N exceeds capacity, lookups miss, the recovery penalty is paid, and per-site cost steps to a higher plateau. The cliff position is the structure's capacity.

### 5.3 Sweep axes

| Axis | Values | Purpose |
|---|---|---|
| `branches` (N) | 1 → 2¹⁵, log2 step | primary cliff search (X axis of the plot) |
| `spacing_bytes` | {16, 32, 64, 128} | secondary — varies which BTB sets are hit, exposes indexing behavior |

Typical run: `ferret run direct_branch_footprint --branches=1..32768 --spacing=64 --out=btb.csv`.

CLI override syntax for axis values:
- `--axis=lo..hi` — range using the axis's declared step policy (e.g. `--branches=1..32768` expands to log2 steps because `branches` is declared `log2_range`).
- `--axis=v1,v2,...` — explicit value list (e.g. `--branches=1,2,4,8` or `--spacing=64`).
- omitted axis — full default range from `axes()`.

### 5.4 Class shape

```cpp
struct DirectBranchFootprint : Benchmark {
  SweepAxes axes() const override {
    return {
      Axis::log2_range("branches", 1, 1 << 15),
      Axis::values("spacing_bytes", {16, 32, 64, 128}),
    };
  }
  size_t sites_per_kernel(const Params& p) const override {
    return p.get<size_t>("branches");
  }
  size_t iterations(const Params& p) const override {
    return std::max<size_t>(1, 10'000'000 / p.get<size_t>("branches"));
  }
  void emit_kernel(sljit_compiler* c, const Params& p) override;
};
FERRET_BENCHMARK("direct_branch_footprint", DirectBranchFootprint);
```

`iterations()` aims for roughly ~10ms of work per measurement at expected per-branch costs, traded off against `branches`. `emit_kernel()` uses sljit to emit the labeled jump chain plus padding plus the outer `iters` loop, leaving label-fixup bookkeeping to a small helper.

## 6. Output

### 6.1 CSV schema

One row per parameter point. Columns:

| Column | Meaning |
|---|---|
| `benchmark` | Benchmark name (e.g., `direct_branch_footprint`) |
| `<axis cols>` | One column per axis defined by the benchmark (e.g., `branches`, `spacing_bytes`) |
| `ticks_min` | Minimum measured tick delta over K repetitions |
| `ticks_median` | Median tick delta over K repetitions |
| `iters` | Kernel iterations baked into the JIT'd code per `fn()` call |
| `sites_per_iter` | Operations per kernel iteration (= `sites_per_kernel(p)`) |
| `reps` | Number of measurement repetitions K |
| `ticks_per_site_min` | `ticks_min / (iters * sites_per_iter)` — the primary Y-axis value |
| `ticks_per_site_median` | `ticks_median / (iters * sites_per_iter)` — sanity check |

Failed JIT (rare) writes a row with empty `ticks_*` columns — the sweep continues.

### 6.2 Plot script

`scripts/plot.py` (matplotlib + pandas) reads the CSV, picks one column as X (the swept axis), `ticks_per_site_min` as Y, and any remaining axis columns as series. Default invocation: `python scripts/plot.py btb.csv`. Script is independent of ferret's binary and runs against any conformant CSV.

## 7. Error handling

Three failure classes:

1. **Configuration errors** (caught by CLI11 or pre-flight checks). Unknown benchmark name, malformed range, missing required axis. Print actionable message, exit non-zero, no partial output.

2. **JIT failures.** sljit can fail on rare paths (out of memory, unsupported operation). The runner catches per-row, logs `sljit_error: <code>` to stderr, writes a CSV row with empty `ticks_*` columns, and continues to the next param point. One bad point does not abort the sweep.

3. **System-state non-fatals.** Pinning advisory on macOS, `mlockall` permission denied on Android non-root, etc. Log a one-line warning to stderr and continue. Per the system-state decision, ferret never refuses to run.

The runner does **not** wrap `fn()` in `try`/`catch`. If a generated kernel segfaults, the process dies — that is a benchmark bug, not a runtime to recover from.

## 8. Testing

### 8.1 Unit tests (GoogleTest, CTest-discovered)

- `test_sweep` — axis expansion: range, log2-range, value-list; cross-product of axes; CLI override semantics.
- `test_timing` — `arch_now_ticks()` is monotonic, two consecutive reads differ by ≥ 0 and < a sane upper bound, tick→ns calibration converges.
- `test_runner` — given a fake `KernelFn` that consumes a known interval, min-of-K is correct within tolerance.
- `test_csv` — schema written, rows correctly formatted, empty-`ticks_*` rows for failed JIT.
- `test_benchmark_registry` — `FERRET_BENCHMARK` macro registers, lookup-by-name works, missing name returns null.

### 8.2 Integration test

One end-to-end smoke: run `direct_branch_footprint --branches=1,2,4,8 --spacing=64 --out=/tmp/x.csv`, assert the CSV has 4 rows with monotonically non-zero `ticks_per_site_min`. Shipped as a CTest entry.

### 8.3 Manual validation (not in CI)

On a known x86 host (Skylake/Zen3 box), run with `--branches=1..32768`, eyeball the cliff position against documented BTB capacity. This is the "did the framework actually measure the right thing" check; not automatable in shared CI runners due to host-hardware variation and VM noise.

CI runs unit + integration tests on every matrix entry (Ubuntu x86_64, Ubuntu ARM64, macOS Apple Silicon) and the Nix job. CI does not assert on cliff positions.

## 9. Future work (out of v1)

- Additional benchmarks: `indirect_branch_footprint`, `return_call_depth`, `conditional_pattern_history`, `branch_alignment_sweep`, ITLB and decoded-uop-cache probes.
- Optional PMU secondary signal (cross-validation against timing).
- RISC-V and LoongArch backend enablement (sljit covers them; the work is build matrix + testing).
- Android NDK cross-compile in CI.
- Config-driven sweeps for end-users who want to vary parameters without recompiling.

# Writing a Benchmark

A new Ferret benchmark is one C++ file under `benchmarks/`. It
subclasses `ferret::Benchmark`, overrides six pure virtuals, and
registers itself at file scope. The runner does the rest.

## The vtable

`Benchmark` (declared in `include/ferret/benchmark.hpp`) requires:

- **`name()`** — must equal the string passed to `FERRET_BENCHMARK`.
  `ferret list` and `ferret run <name>` both look it up by this string.
- **`axes()`** — returns the swept axes. Order is significant: the
  first axis varies slowest in the CSV output and the first column to
  the right of `benchmark` is the first axis. Use
  `Axis::range(...)`, `Axis::log2_range(...)`, or
  `Axis::values(...)` (declared in `include/ferret/axis.hpp`).
- **`options()`** — returns scalar non-swept knobs. Each appears as a
  `--<name>=<v>` CLI flag and is recorded in the CSV one column after
  the axes. Default to `{}` if your benchmark has no per-bench options.
- **`sites_per_kernel(p)`** — divisor the runner uses to convert
  `ticks` into per-site latency. Must be > 0; the runner rejects zero
  with exit code 2.
- **`iterations(p)`** — outer-loop count compiled into the kernel.
  Amortizes the tick-read overhead at the runner level. Must be > 0.
- **`emit_kernel(c, p)`** — emits the actual measurement kernel into
  `c` via sljit. The kernel must end with a return. If a parameter
  point is invalid at the ISA level (e.g., `spacing_bytes` too small),
  `throw std::invalid_argument` *before* touching `c` so the compiler
  state stays clean. Any sljit error set on `c` propagates to
  `JittedKernel::ok() == false`.

## Registration

At the bottom of the `.cpp` file, after the `struct` definition:

```cpp
FERRET_BENCHMARK("my_bench", MyBench);
```

The macro registers a factory at static-init time, so `ferret list`
sees the new name automatically. Add the new file to the executable's
source list in the top-level `CMakeLists.txt`:

```cmake
add_executable(ferret
  src/main.cpp
  benchmarks/dependent_chain_throughput.cpp
  benchmarks/direct_branch_footprint.cpp
  benchmarks/my_bench.cpp        # <-- new
)
```

## Worked example A — frequency-probe pattern

`benchmarks/dependent_chain_throughput.cpp` is the simplest possible
benchmark: a single-valued axis and a one-shot kernel.

```cpp
struct DependentChainThroughput : Benchmark {
  static constexpr int UNROLL = 1024;
  [[nodiscard]] std::string name() const override { return "dependent_chain_throughput"; }
  [[nodiscard]] SweepAxes axes() const override {
    return {Axis::values("chain_length", {100'000'000})};
  }
  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override {
    return p.get<size_t>("chain_length");
  }
  [[nodiscard]] size_t iterations(const Params& /*p*/) const override { return 1; }
  // ... emit_kernel emits chain_length dependent ADDs ...
};
```

Why each choice:

- **`iterations = 1`**: the kernel itself executes `chain_length` ops;
  no outer loop is needed.
- **`sites_per_kernel = chain_length`**: the runner divides ticks by
  `iterations * sites_per_kernel` = `chain_length`, yielding
  ns-per-op = ns-per-cycle on a 1-IPC core (every ADD is dependent on
  the previous, so IPC is exactly 1 on every common high-perf or
  in-order ARM core).
- **`UNROLL = 1024`**: the inner loop body is a 1024-ADD straight-line
  block; the outer loop runs `chain_length / 1024` times plus a
  straight-line tail of `chain_length % 1024` ADDs. Total ops per
  `fn()` invocation == `chain_length` exactly.

## Worked example B — parameter-sweep pattern

`benchmarks/direct_branch_footprint.cpp` has two axes and one option,
plus ISA-level pre-flight validation.

```cpp
struct DirectBranchFootprint : Benchmark {
  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::log2_range("branches", 1, 1 << 15),
        Axis::log2_range("spacing_bytes", 16, 128),
    };
  }
  [[nodiscard]] BenchOptions options() const override {
    return {BenchOption{.name = "sattolo_permute", .default_value = 0}};
  }
  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override {
    return p.get<size_t>("branches");
  }
  [[nodiscard]] size_t iterations(const Params& p) const override {
    return std::max<size_t>(1, 10'000'000 / p.get<size_t>("branches"));
  }
  void emit_kernel(sljit_compiler* c, const Params& p) override {
    auto spacing = p.get<size_t>("spacing_bytes");
    if (spacing < kMinBranchBytes) {
      throw std::invalid_argument("spacing_bytes=...");  // before touching `c`
    }
    // ... emit branches, optionally Sattolo-permuted ...
  }
};
```

Key idioms:

- **Two `log2_range` axes** — `branches=1..32768` expands to
  `{1, 2, 4, 8, ..., 32768}`; `spacing_bytes=16..128` to
  `{16, 32, 64, 128}`. The framework takes the cartesian product.
- **`sattolo_permute` option** — surfaced as `--sattolo_permute=0|1`.
  Stored in every CSV row, optionally rewires branch targets as a
  uniform random Hamiltonian cycle (see `permute.hpp`).
- **`iterations` adapts to `branches`** — capped at 10 M ops total per
  kernel invocation so smaller `branches` values still get reasonable
  measurement counts.
- **ISA validation before any sljit state changes** — the alignment and
  minimum-encoding checks happen up front; bad params throw cleanly and
  leave no partial compiler state behind.
- **`emit_nops(c, n)`** — pads each branch out to the requested
  `spacing_bytes` using arch-correct NOP encodings (see `padding.hpp`).

## Smoke-testing a new benchmark

After building:

```sh
build/ferret list                                     # confirm registration
build/ferret run my_bench --reps=3 --warmup=1         # confirm it runs end-to-end
```

If the new benchmark has axes, supply explicit overrides while
iterating (`--my_axis=1,2,4`) so the smoke run finishes in seconds.

The framework's own integration suite picks up new benchmarks
automatically — `ctest --test-dir build --output-on-failure` will
exercise registration. If you want behavior-level coverage, add a
gtest under `tests/` linking `ferret_core`.

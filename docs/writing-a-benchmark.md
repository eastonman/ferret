# Writing a Benchmark

A new Ferret benchmark is one C++ file under `benchmarks/`. It
subclasses `ferret::Benchmark`, overrides five pure virtuals (plus two
optional virtual hooks), and registers itself at file scope.
The runner does the rest.

## The vtable

`Benchmark` (declared in `include/ferret/benchmark.hpp`) requires:

- **`name()`** — must equal the string passed to `FERRET_BENCHMARK`.
  `ferret list` and `ferret run <name>` both look it up by this string.
- **`axes()`** — returns the swept axes. Order is significant: the
  first axis varies slowest in the CSV output and the first column to
  the right of `benchmark` is the first axis. Use
  `Axis::range(...)`, `Axis::log2_range(...)`,
  `Axis::geom_range(name, lo, hi, samples_per_octave)`, or
  `Axis::values(...)` (declared in `include/ferret/axis.hpp`).
  `geom_range` is `log2_range` when `samples_per_octave == 1`; pick
  a larger `k` when the capacity cliff under test sits between two
  adjacent powers of two and you want denser default sampling.
- **`options()`** _(optional, defaults to `{}`)_ — returns scalar
  non-swept knobs. Each appears as a `--<name>=<v>` CLI flag and is
  recorded in the CSV one column after the axes. Override only when
  your benchmark exposes per-bench options.
- **`sites_per_kernel(p)`** — divisor the runner uses to convert
  `ticks` into per-site latency. Must be > 0; the runner rejects zero
  with exit code 2.
- **`iterations(p)`** — outer-loop count compiled into the kernel.
  Amortizes the tick-read overhead at the runner level. Must be > 0.
- **`emit_kernel(c, p)`** — emits the actual measurement kernel into
  `c` via sljit. The kernel must end with a return. If a parameter
  point is invalid at the ISA level (e.g., `spacing_bytes` too small),
  `throw std::invalid_argument` _before_ touching `c` so the compiler
  state stays clean. Any sljit error set on `c` propagates to
  `JittedKernel::ok() == false`.
- **`verify_layout(c)`** _(optional, defaults to no-op)_ — called once
  per row after `sljit_generate_code` while the compiler is still
  alive. Label addresses are only valid in that window, and post-
  generate patches need `sljit_get_executable_offset(c)` to find the
  writable mapping. Override to assert layout invariants (e.g., that
  branch sites landed at the expected spacing). Throw on mismatch;
  the runner records the row as a JIT failure.

## Registration

At the bottom of the `.cpp` file, after the `struct` definition:

```cpp
FERRET_BENCHMARK("my_bench", MyBench);
```

The macro registers a factory at static-init time, so `ferret list`
sees the new name automatically. Add the new file to the
`ferret_benchmarks` OBJECT library in the top-level `CMakeLists.txt`
— that library is reused by the `ferret` executable and by benchmark-
specific tests:

```cmake
add_library(ferret_benchmarks OBJECT
  benchmarks/branch_history_footprint.cpp
  benchmarks/direct_branch_footprint.cpp
  benchmarks/nested_call_depth.cpp
  benchmarks/dependent_chain_throughput.cpp
  benchmarks/my_bench.cpp        # <-- new
)
```

## Shared helpers — `bench_helpers.hpp`

`include/ferret/bench_helpers.hpp` provides three JIT-time utilities
that the existing benchmarks all use. The header pulls in the full
`sljitLir.h`, so include it only from benchmark TUs and other JIT-only
sources, not from public headers.

- **`emit_outer_loop(c, counter_reg, iters, emit_body)`** — emits the
  canonical scaffold (`MOV counter, iters` → loop label → body →
  `SUB|SET_Z counter, 1` → `JNZ`). `counter_reg` must be a scratch
  register the body neither reads nor writes. Prefer this over hand-
  rolling a counter loop in `emit_kernel` so the runner-visible loop
  shape stays consistent across benchmarks.
- **`compute_iterations(target_ops, sites_per_kernel)`** — returns
  `max(1, target_ops / sites_per_kernel)`. Use it from `iterations()`
  to pick an outer-loop count that amortizes the tick-read overhead to
  roughly `target_ops` ops per repetition.
- **`verify_uniform_spacing(labels, spacing, strict, context)`** — call
  from `verify_layout` to assert each label sits at `i * spacing` off
  `labels[0]`. `strict=true` requires equality (used by
  `direct_branch_footprint`); `strict=false` requires `>=` (sljit may
  pick a longer encoding, so spacing is a floor —
  `branch_history_footprint` uses this mode). Throws
  `std::runtime_error` with the per-site delta on the first mismatch.

The "frequency-probe pattern" example below sets `iterations=1` and so
does not need `emit_outer_loop`; the "parameter-sweep pattern" example
and the other benchmarks in `benchmarks/` use all three helpers.

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
        Axis::geom_range("branches", 1, 1 << 10, /*samples_per_octave=*/1),
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

- **A `geom_range` and a `log2_range` axis** — `branches=1..1024`
  expands to `{1, 2, 4, 8, ..., 1024}` (with `samples_per_octave=1`,
  identical to `log2_range`; pass `--branches=lo..hi@k` to densify);
  `spacing_bytes=16..128` to `{16, 32, 64, 128}`. The framework takes
  the cartesian product.
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

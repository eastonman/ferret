#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ferret/axis.hpp"
#include "ferret/params.hpp"
#include "ferret/runner.hpp"

// Forward-declare sljit_compiler so headers don't pull in sljitLir.h
// transitively. Implementations include sljitLir.h directly.
struct sljit_compiler;

namespace ferret {

// Scalar per-benchmark option: not swept, surfaced as `--<name>=<v>`,
// injected into every Params row and emitted as a CSV column after axes.
struct BenchOption {
  std::string name;
  int64_t default_value;
};

using BenchOptions = std::vector<BenchOption>;

// Base class for one microbenchmark. Subclasses are constructed via
// BenchmarkRegistry::create. The runner calls axes() and options() once
// at startup; then, for every parameter point produced by sweep::expand,
// it calls sites_per_kernel(), iterations(), and emit_kernel() in that
// order, JIT-compiles the kernel, and times it via runner::measure.
class Benchmark {
 public:
  virtual ~Benchmark() = default;

  // Must equal the string passed to FERRET_BENCHMARK.
  virtual std::string name() const = 0;

  // Order is significant — defines CSV column order and the sweep-nesting
  // (the first axis varies slowest in the output).
  virtual SweepAxes axes() const = 0;

  // Scalar non-swept knobs surfaced as `--<name>=<v>`. Default is {} for
  // benchmarks with no per-bench options.
  virtual BenchOptions options() const { return {}; }

  // Per-site normalization divisor the runner divides ticks by. Must be
  // > 0; zero is rejected with exit code 2.
  virtual size_t sites_per_kernel(const Params& p) const = 0;

  // Outer-loop iteration count compiled into the kernel to amortize the
  // runner's tick-read overhead. Must be > 0.
  virtual size_t iterations(const Params& p) const = 0;

  // Emit the benchmark kernel into `c`. The kernel must end with a
  // return. Any sljit error set on `c` propagates to
  // JittedKernel::ok() == false. ISA-level parameter rejections should
  // `throw std::invalid_argument` *before* mutating `c` (see the
  // kMinBranchBytes / kBranchAlign validation in
  // direct_branch_footprint.cpp).
  virtual void emit_kernel(sljit_compiler* c, const Params& p) = 0;

  // Called once per row after sljit_generate_code, while the compiler is
  // still alive (label addresses are only valid in that window, and post-
  // generate patches need sljit_get_executable_offset(c) to find the
  // writable mapping). Override to validate layout invariants or to
  // resolve hand-emitted jump displacements. Throw on mismatch; the
  // runner emits the row as a JIT failure.
  virtual void verify_layout(sljit_compiler* c) { (void)c; }

  // Per-row measurement strategy. Most benchmarks build one JIT'd
  // kernel and time it; they implement this as a one-liner that calls
  // runner::single_kernel_measure(*this, p, reps, warmup). Benchmarks
  // that need a richer strategy (e.g. train_betray_latency, which times
  // two kernels and reports the difference) override directly.
  virtual MeasurementRow measure_row(const Params& p, int reps, int warmup) = 0;
};

// Static singleton registry. Benchmark subclasses normally register
// themselves at file scope via the FERRET_BENCHMARK macro; the registry
// is then queried by `ferret list` and `ferret run`.
class BenchmarkRegistry {
 public:
  using Factory = std::function<std::unique_ptr<Benchmark>()>;

  static void register_benchmark(std::string name, Factory factory);
  static std::unique_ptr<Benchmark> create(const std::string& name);

  // Returns the registered names sorted lexicographically.
  static std::vector<std::string> names();
};

}  // namespace ferret

// Registers a Benchmark subclass under a string name. Place at file
// scope in any .cpp under benchmarks/.
#define FERRET_BENCHMARK(NAME, CLASS)                                              \
  namespace {                                                                      \
  const bool _ferret_register_##CLASS = []() {                                     \
    ::ferret::BenchmarkRegistry::register_benchmark(                               \
        NAME, []() { return std::unique_ptr<::ferret::Benchmark>(new CLASS()); }); \
    return true;                                                                   \
  }();                                                                             \
  }

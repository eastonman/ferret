#pragma once

#include <cstddef>
#include <cstdint>

namespace ferret {
class Benchmark;
class Params;
}  // namespace ferret

namespace ferret {

struct MeasurementRow {
  uint64_t ticks_min = 0;
  uint64_t ticks_median = 0;
  size_t iters = 0;
  size_t sites = 0;
  size_t reps = 0;
  bool jit_failed = false;
};

namespace runner {

using KernelFn = void (*)(void);

struct MeasureOptions {
  size_t iters;
  size_t sites;
  int reps;
  int warmup;
};

// Runs `opts.warmup` un-timed calls, then `opts.reps` timed calls.
// Returns ticks_min, ticks_median over the timed samples plus the
// iters/sites/reps fields for accounting. iters and sites are passed
// through to MeasurementRow purely for normalization downstream — they
// are not used by measure() to drive iteration count.
MeasurementRow measure(KernelFn fn, const MeasureOptions& opts);

// Builds a JittedKernel for `b` at `p`, runs runner::measure on it, and
// fills the resulting MeasurementRow with iters / sites taken from the
// benchmark. The default measure_row strategy for benchmarks that time
// a single JIT'd kernel — differencing benchmarks override measure_row
// directly and don't use this helper.
//
// On JIT failure, returns a row with .jit_failed = true and
// .iters/.sites pre-populated. Propagates std::exception from
// emit_kernel/verify_layout to the caller.
MeasurementRow single_kernel_measure(Benchmark& b, const Params& p, int reps, int warmup);

}  // namespace runner
}  // namespace ferret

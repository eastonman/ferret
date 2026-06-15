#pragma once

#include <cstddef>
#include <cstdint>

namespace ferret {

struct MeasurementRow {
  uint64_t ticks_min = 0;
  uint64_t ticks_median = 0;
  size_t iters = 0;
  size_t sites = 0;  // emitted in CSV as the `sites_per_iter` column (see CsvWriter::write_header)
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

}  // namespace runner
}  // namespace ferret

#pragma once

#include <cstddef>
#include <cstdint>

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

// Runs `warmup` un-timed calls, then K timed calls. Returns ticks_min,
// ticks_median over the K samples plus the iters/sites/reps for accounting.
// iters and sites are not used by measure() to drive iteration count —
// they are passed through to MeasurementRow purely for normalization
// downstream.
MeasurementRow measure(KernelFn fn, size_t iters, size_t sites,
                       int K, int warmup);

}  // namespace runner
}  // namespace ferret

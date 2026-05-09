#pragma once

#include <cstdint>

namespace ferret::timing {

// Lowest-overhead user-mode tick counter for the current architecture.
// Returns a free-running counter at fixed frequency. Not necessarily
// monotonic in pathological cases (counter wraparound, thread migration
// across cores with skewed counters), but on supported targets the
// counter is per-core invariant.
uint64_t arch_now_ticks();

// Ratio ticks-per-nanosecond. Calibrated lazily on first call:
//   - x86_64: TSC frequency measured against CLOCK_MONOTONIC_RAW over ~10 ms.
//   - aarch64: read from cntfrq_el0 directly.
double ticks_per_ns();

}  // namespace ferret::timing

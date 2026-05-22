#include "ferret/timing.hpp"

#include <chrono>
#include <thread>

#if defined(__linux__)
#include <sched.h>

#include "ferret/log.hpp"
#include "ferret/pinning.hpp"
#endif

namespace ferret::timing {

namespace {
double calibrate() {
#if defined(__aarch64__) || defined(_M_ARM64)
  uint64_t hz;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(hz));
  return static_cast<double>(hz) / 1e9;
#else
  // Pin to the current core before sampling to reduce TSC skew from
  // cross-core migration or frequency scaling during the sleep window.
  // Calibration accuracy depends on the caller having stable affinity; if
  // pinning fails, the measurement proceeds but a post-sleep migration check
  // will warn if the thread moved.
#if defined(__linux__)
  int cpu_before = sched_getcpu();
  if (cpu_before >= 0) {
    if (!ferret::pinning::pin_to_core(cpu_before)) {
      spdlog::warn("TSC calibration: could not pin thread to core {}; frequency estimate may be imprecise", cpu_before);
    }
  }
#endif

  using clock = std::chrono::steady_clock;
  auto wall_start = clock::now();
  uint64_t t0 = arch_now_ticks();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  uint64_t t1 = arch_now_ticks();
  auto wall_end = clock::now();

#if defined(__linux__)
  if (cpu_before >= 0) {
    int cpu_after = sched_getcpu();
    if (cpu_after >= 0 && cpu_after != cpu_before) {
      spdlog::warn(
          "TSC calibration: thread migrated from core {} to core {} during measurement; frequency estimate may be "
          "imprecise",
          cpu_before, cpu_after);
    }
  }
#endif

  double wall_ns = std::chrono::duration<double, std::nano>(wall_end - wall_start).count();
  return static_cast<double>(t1 - t0) / wall_ns;
#endif
}
}  // namespace

double ticks_per_ns() {
  static const double v = calibrate();
  return v;
}

}  // namespace ferret::timing

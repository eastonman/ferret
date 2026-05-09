#include "ferret/timing.hpp"

#include <chrono>
#include <thread>

namespace ferret::timing {

namespace {
double calibrate() {
#if defined(__aarch64__) || defined(_M_ARM64)
  uint64_t hz;
  asm volatile("mrs %0, cntfrq_el0" : "=r"(hz));
  return static_cast<double>(hz) / 1e9;
#else
  using clock = std::chrono::steady_clock;
  auto wall_start = clock::now();
  uint64_t t0 = arch_now_ticks();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  uint64_t t1 = arch_now_ticks();
  auto wall_end = clock::now();
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

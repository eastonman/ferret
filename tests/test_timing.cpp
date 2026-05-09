#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "ferret/timing.hpp"

using namespace ferret;

TEST(Timing, ArchNowTicksMonotonic) {
  uint64_t a = timing::arch_now_ticks();
  uint64_t b = timing::arch_now_ticks();
  EXPECT_GE(b, a);
}

TEST(Timing, TwoConsecutiveReadsAreCloseInTime) {
  uint64_t a = timing::arch_now_ticks();
  uint64_t b = timing::arch_now_ticks();
  uint64_t delta = b - a;
  EXPECT_LT(delta, static_cast<uint64_t>(1'000'000'000));
}

TEST(Timing, TicksPerNsIsPositive) {
  double t = timing::ticks_per_ns();
  EXPECT_GT(t, 0.0);
  EXPECT_LT(t, 100.0);
}

TEST(Timing, TenMillisecondsRoughlyMatchesWallClock) {
  using clock = std::chrono::steady_clock;
  auto wall_start = clock::now();
  uint64_t t0 = timing::arch_now_ticks();
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  uint64_t t1 = timing::arch_now_ticks();
  auto wall_end = clock::now();

  double observed_ns = (t1 - t0) / timing::ticks_per_ns();
  double wall_ns = std::chrono::duration<double, std::nano>(wall_end - wall_start).count();

  EXPECT_GT(observed_ns, wall_ns * 0.7);
  EXPECT_LT(observed_ns, wall_ns * 1.3);
}

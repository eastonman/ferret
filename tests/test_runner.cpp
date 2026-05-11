#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "ferret/runner.hpp"

using namespace ferret;

namespace {
void slow_kernel_100us() { std::this_thread::sleep_for(std::chrono::microseconds(100)); }
void noop_kernel() {}
}  // namespace

TEST(Runner, MinOfKChoosesShortestSample) {
  MeasurementRow m = runner::measure(slow_kernel_100us, /*iters=*/1,
                                     /*sites=*/1, /*K=*/5, /*warmup=*/1);
  EXPECT_GT(m.ticks_min, 0u);
  EXPECT_GE(m.ticks_median, m.ticks_min);
  EXPECT_EQ(m.iters, 1u);
  EXPECT_EQ(m.sites, 1u);
  EXPECT_EQ(m.reps, 5u);
  EXPECT_FALSE(m.jit_failed);
}

TEST(Runner, NoopKernelHasZeroOrTinyMin) {
  MeasurementRow m = runner::measure(noop_kernel, 1, 1, 5, 1);
  EXPECT_FALSE(m.jit_failed);
  EXPECT_GE(m.ticks_median, m.ticks_min);
}

TEST(Runner, KernelInvokedAtLeastWarmupPlusKTimes) {
  static int counter;
  counter = 0;
  auto count = []() { ++counter; };
  runner::measure(count, 1, 1, /*K=*/4, /*warmup=*/2);
  EXPECT_EQ(counter, 2 + 4);
}

TEST(Runner, RejectsNonPositiveK) {
  EXPECT_THROW(runner::measure(noop_kernel, 1, 1, /*K=*/0, /*warmup=*/1), std::invalid_argument);
  EXPECT_THROW(runner::measure(noop_kernel, 1, 1, /*K=*/-1, /*warmup=*/1), std::invalid_argument);
}

TEST(Runner, RejectsNegativeWarmup) {
  EXPECT_THROW(runner::measure(noop_kernel, 1, 1, /*K=*/3, /*warmup=*/-1), std::invalid_argument);
}

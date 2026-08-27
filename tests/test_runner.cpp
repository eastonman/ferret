#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include "ferret/benchmark.hpp"
#include "ferret/jit.hpp"
#include "ferret/params.hpp"
#include "ferret/runner.hpp"

extern "C" {
#include <sljitLir.h>
}

using namespace ferret;

namespace {
void slow_kernel_100us() { std::this_thread::sleep_for(std::chrono::microseconds(100)); }
void noop_kernel() {}
}  // namespace

TEST(Runner, MinOfKChoosesShortestSample) {
  MeasurementRow m = runner::measure(slow_kernel_100us, {.iters = 1, .sites = 1, .reps = 5, .warmup = 1});
  EXPECT_GT(m.ticks_min, 0u);
  EXPECT_GE(m.ticks_median, m.ticks_min);
  EXPECT_EQ(m.iters, 1u);
  EXPECT_EQ(m.sites, 1u);
  EXPECT_EQ(m.reps, 5u);
  EXPECT_FALSE(m.jit_failed);
}

TEST(Runner, NoopKernelHasZeroOrTinyMin) {
  MeasurementRow m = runner::measure(noop_kernel, {.iters = 1, .sites = 1, .reps = 5, .warmup = 1});
  EXPECT_FALSE(m.jit_failed);
  EXPECT_GE(m.ticks_median, m.ticks_min);
}

TEST(Runner, KernelInvokedAtLeastWarmupPlusKTimes) {
  static int counter;
  counter = 0;
  auto count = []() { ++counter; };
  runner::measure(count, {.iters = 1, .sites = 1, .reps = 4, .warmup = 2});
  EXPECT_EQ(counter, 2 + 4);
}

TEST(Runner, RejectsNonPositiveK) {
  EXPECT_THROW(runner::measure(noop_kernel, {.iters = 1, .sites = 1, .reps = 0, .warmup = 1}), std::invalid_argument);
  EXPECT_THROW(runner::measure(noop_kernel, {.iters = 1, .sites = 1, .reps = -1, .warmup = 1}), std::invalid_argument);
}

TEST(Runner, RejectsNegativeWarmup) {
  EXPECT_THROW(runner::measure(noop_kernel, {.iters = 1, .sites = 1, .reps = 3, .warmup = -1}), std::invalid_argument);
}

namespace {
struct NoopBenchmark : public ferret::Benchmark {
  std::string name() const override { return "noop"; }
  ferret::SweepAxes axes() const override { return {}; }
  size_t sites_per_kernel(const ferret::Params&) const override { return 1; }
  size_t iterations(const ferret::Params&) const override { return 1; }
  void emit_kernel(sljit_compiler* c, const ferret::Params&) override {
    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 1, 0, 0);
    sljit_emit_return_void(c);
  }
  ferret::MeasurementRow measure_row(const ferret::Params& p, int reps, int warmup) override {
    return ferret::runner::single_kernel_measure(*this, p, reps, warmup);
  }
};
}  // namespace

TEST(SingleKernelMeasure, RunsNoopBenchmarkSuccessfully) {
  NoopBenchmark b;
  ferret::Params p;
  auto row = ferret::runner::single_kernel_measure(b, p, /*reps=*/3, /*warmup=*/1);
  EXPECT_FALSE(row.jit_failed);
  EXPECT_EQ(row.sites, 1u);
  EXPECT_EQ(row.iters, 1u);
  EXPECT_EQ(row.reps, 3u);
  EXPECT_GE(row.ticks_median, row.ticks_min);
}

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include "ferret/benchmark.hpp"
#include "ferret/params.hpp"
#include "sljit_test_helpers.hpp"

using ferret::testing::find_option;

namespace {
ferret::Params make_params(int64_t branches, int64_t train_iters = 8, int64_t spacing = 16) {
  ferret::Params p;
  p.set("branches", branches);
  p.set("train_iters", train_iters);
  p.set("spacing_bytes", spacing);
  p.set("seed", 1);
  return p;
}
}  // namespace

TEST(TrainBetrayLatency, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "train_betray_latency");
}

TEST(TrainBetrayLatency, ExposesBranchesAxis) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  ASSERT_EQ(axes.size(), 1u);
  EXPECT_EQ(axes[0].name(), "branches");
}

TEST(TrainBetrayLatency, BranchesAxisDefaultRangeMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[0].expand();
  // 16384..65536 with k=1: {16384, 32768, 65536} = 3 points.
  EXPECT_EQ(vs.size(), 3u);
  EXPECT_EQ(vs.front(), 16384);
  EXPECT_EQ(vs.back(), 65536);
}

TEST(TrainBetrayLatency, ExposesTrainItersAndSpacingBytesOptions) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 2u);

  const auto* train = find_option(opts, "train_iters");
  ASSERT_NE(train, nullptr);
  EXPECT_EQ(train->default_value, 8);

  const auto* spacing = find_option(opts, "spacing_bytes");
  ASSERT_NE(spacing, nullptr);
  EXPECT_EQ(spacing->default_value, 16);
}

TEST(TrainBetrayLatency, SitesPerKernelEqualsBranches) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->sites_per_kernel(make_params(256)), 256u);
  EXPECT_EQ(b->sites_per_kernel(make_params(1024)), 1024u);
}

TEST(TrainBetrayLatency, IterationsEqualsTrainItersPlusOne) {
  // One kernel call runs exactly one train-and-betray cycle:
  // train_iters training rounds + 1 betrayal round. Anything more would
  // give TAGE the chance to learn the period.
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->iterations(make_params(256, /*train_iters=*/2)), 3u);
  EXPECT_EQ(b->iterations(make_params(1024, /*train_iters=*/8)), 9u);
}

namespace ferret::train_betray_latency_internal {
enum class FillMode : std::uint8_t { Betray, Control };
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t train_iters, FillMode mode);
}  // namespace ferret::train_betray_latency_internal

// If FillMode in benchmarks/train_betray_latency.cpp gains or reorders
// values, this static_assert catches the shadow-declaration drift.
static_assert(static_cast<int>(ferret::train_betray_latency_internal::FillMode::Betray) == 0);
static_assert(static_cast<int>(ferret::train_betray_latency_internal::FillMode::Control) == 1);

TEST(TrainBetrayLatency, BetrayFillTrainRowsAreOnesBetrayRowIsZeros) {
  using namespace ferret::train_betray_latency_internal;
  const size_t K = 4;
  const size_t M = 3;
  auto v = generate_pattern_fill(K, M, FillMode::Betray);
  ASSERT_EQ(v.size(), K * (M + 1));
  for (size_t row = 0; row < M; ++row) {
    for (size_t j = 0; j < K; ++j) {
      EXPECT_EQ(v[row * K + j], 1u) << "training row " << row << " col " << j;
    }
  }
  for (size_t j = 0; j < K; ++j) {
    EXPECT_EQ(v[M * K + j], 0u) << "betrayal row col " << j;
  }
}

TEST(TrainBetrayLatency, ControlFillAllRowsAreOnes) {
  using namespace ferret::train_betray_latency_internal;
  const size_t K = 4;
  const size_t M = 3;
  auto v = generate_pattern_fill(K, M, FillMode::Control);
  ASSERT_EQ(v.size(), K * (M + 1));
  for (uint32_t x : v) EXPECT_EQ(x, 1u);
}

TEST(TrainBetrayLatency, FillHandlesTrainItersZero) {
  using namespace ferret::train_betray_latency_internal;
  // M=0: Betray collapses to a single all-0 row; Control to a single all-1 row.
  auto betray = generate_pattern_fill(4, 0, FillMode::Betray);
  ASSERT_EQ(betray.size(), 4u);
  for (uint32_t x : betray) EXPECT_EQ(x, 0u);
  auto control = generate_pattern_fill(4, 0, FillMode::Control);
  ASSERT_EQ(control.size(), 4u);
  for (uint32_t x : control) EXPECT_EQ(x, 1u);
}

namespace ferret::train_betray_latency_internal {
struct LayoutSnapshot {
  std::vector<sljit_label*> labels;
  size_t branches;
  size_t spacing;
};
LayoutSnapshot last_layout_snapshot();
}  // namespace ferret::train_betray_latency_internal

TEST(TrainBetrayLatency, RejectsZeroBranches) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  ferret::testing::CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(0)), std::invalid_argument);
}

TEST(TrainBetrayLatency, RejectsNegativeTrainIters) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  ferret::testing::CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(4, /*train_iters=*/-1)), std::invalid_argument);
}

TEST(TrainBetrayLatency, RejectsSpacingBytesTooSmall) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  ferret::testing::CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(4, /*train_iters=*/32, /*spacing=*/4)), std::invalid_argument);
}

TEST(TrainBetrayLatency, EmitsValidKernelForSmallParams) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  ferret::testing::CompilerHandle ch;
  auto p = make_params(/*branches=*/4, /*train_iters=*/8, /*spacing=*/16);
  ASSERT_NO_THROW(b->emit_kernel(ch.c, p));
  ASSERT_EQ(sljit_get_compiler_error(ch.c), SLJIT_SUCCESS);
  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  ASSERT_NO_THROW(b->verify_layout(ch.c));
  sljit_free_code(code, nullptr);
}

TEST(TrainBetrayLatency, EmitsValidKernelForBranchesEqualsOne) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  ferret::testing::CompilerHandle ch;
  auto p = make_params(/*branches=*/1, /*train_iters=*/8, /*spacing=*/16);
  ASSERT_NO_THROW(b->emit_kernel(ch.c, p));
  ASSERT_EQ(sljit_get_compiler_error(ch.c), SLJIT_SUCCESS);
  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  ASSERT_NO_THROW(b->verify_layout(ch.c));
  sljit_free_code(code, nullptr);
}

TEST(TrainBetrayLatency, LayoutSnapshotMeetsMinimumSpacing) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  ferret::testing::CompilerHandle ch;
  b->emit_kernel(ch.c, make_params(/*branches=*/4, /*train_iters=*/4, /*spacing=*/16));
  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  b->verify_layout(ch.c);

  auto snap = ferret::train_betray_latency_internal::last_layout_snapshot();
  EXPECT_EQ(snap.branches, 4u);
  EXPECT_EQ(snap.spacing, 16u);
  ASSERT_EQ(snap.labels.size(), 5u);  // branches + 1 chain-exit label
  sljit_uw base = sljit_get_label_addr(snap.labels[0]);
  for (size_t i = 1; i <= snap.branches; ++i) {
    sljit_uw addr = sljit_get_label_addr(snap.labels[i]);
    EXPECT_GE(addr - base, i * snap.spacing) << "site " << i;
  }
  sljit_free_code(code, nullptr);
}

#include "ferret/jit.hpp"

TEST(TrainBetrayLatency, MeasureRowReturnsRowShapedForCsvDivision) {
  auto b = ferret::BenchmarkRegistry::create("train_betray_latency");
  ASSERT_NE(b, nullptr);
  // Small K + default train_iters keeps the test fast; we are checking
  // the wiring (per-trial fresh-emit + differencing returns a sensible
  // row), not the absolute mispredict cost.
  auto p = make_params(/*branches=*/64, /*train_iters=*/2, /*spacing=*/16);
  auto row = b->measure_row(p, /*reps=*/3, /*warmup=*/0);
  EXPECT_FALSE(row.jit_failed);
  EXPECT_EQ(row.sites, 64u);
  EXPECT_EQ(row.iters, 1u);  // CSV divides by iters*sites; iters=1 makes sites=K = "mispredict count"
  // row.reps reports contributing samples (post-warmup, post-zero-filter), which
  // can be < the user-supplied reps when noisy trials clamp. Bound it loosely.
  EXPECT_LE(row.reps, 3u);
  EXPECT_GE(row.ticks_median, row.ticks_min);
}

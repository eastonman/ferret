#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ferret/benchmark.hpp"
#include "ferret/params.hpp"

extern "C" {
#include <sljitLir.h>
}

namespace ferret::bias_conditional_branch_count_internal {
std::vector<uint8_t> assign_directions(size_t branches, int64_t nt_branch_pct, uint64_t seed);
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t pattern_period, int64_t bias_pct,
                                              int64_t nt_branch_pct, uint64_t seed);
struct LayoutSnapshot {
  std::vector<sljit_label*> labels;
  size_t branches;
  size_t spacing;
};
LayoutSnapshot last_layout_snapshot();
}  // namespace ferret::bias_conditional_branch_count_internal

namespace {
ferret::Params make_params(int64_t branches, int64_t total_outcomes, int64_t bias = 95, int64_t nt = 50,
                            int64_t spacing = 16) {
  ferret::Params p;
  p.set("branches", branches);
  p.set("total_outcomes", total_outcomes);
  p.set("bias_pct", bias);
  p.set("nt_branch_pct", nt);
  p.set("spacing_bytes", spacing);
  p.set("seed", 1);
  return p;
}
const ferret::BenchOption* find_option(const ferret::BenchOptions& opts, const std::string& name) {
  for (const auto& o : opts) {
    if (o.name == name) return &o;
  }
  return nullptr;
}
struct CompilerHandle {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  ~CompilerHandle() {
    if (c) sljit_free_compiler(c);
  }
};
}  // namespace

TEST(BiasConditionalBranchCount, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "bias_conditional_branch_count");
}

TEST(BiasConditionalBranchCount, ExposesBranchesAndTotalOutcomesAxes) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  ASSERT_EQ(axes.size(), 2u);
  EXPECT_EQ(axes[0].name(), "branches");
  EXPECT_EQ(axes[1].name(), "total_outcomes");
}

TEST(BiasConditionalBranchCount, BranchesAxisExpansionMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[0].expand();
  // 1..8192 with k=1: {1,2,4,…,8192} = 14 points.
  EXPECT_EQ(vs.size(), 14u);
  EXPECT_EQ(vs.front(), 1);
  EXPECT_EQ(vs.back(), 8192);
}

TEST(BiasConditionalBranchCount, TotalOutcomesAxisExpansionMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[1].expand();
  // 8192..1048576 with k=1: 8 points.
  EXPECT_EQ(vs.size(), 8u);
  EXPECT_EQ(vs.front(), 8192);
  EXPECT_EQ(vs.back(), 1048576);
}

TEST(BiasConditionalBranchCount, DefaultAxisGridIsValidationClean) {
  // total_outcomes_min must be >= branches_max so pattern_period >= 1
  // at every default Cartesian point. spec §5.1.
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  auto branches = axes[0].expand();
  auto totals = axes[1].expand();
  EXPECT_GE(totals.front(), branches.back());
}

TEST(BiasConditionalBranchCount, ExposesAllThreeOptions) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 3u);
  EXPECT_EQ(find_option(opts, "bias_pct")->default_value, 95);
  EXPECT_EQ(find_option(opts, "nt_branch_pct")->default_value, 50);
  EXPECT_EQ(find_option(opts, "spacing_bytes")->default_value, 16);
}

TEST(BiasConditionalBranchCount, SitesPerKernelEqualsBranches) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->sites_per_kernel(make_params(32, 65536)), 32u);
  EXPECT_EQ(b->sites_per_kernel(make_params(1, 8192)), 1u);
}

TEST(BiasConditionalBranchCount, IterationsAmortizesAtTenMillionSites) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->iterations(make_params(1, 8192)), 10'000'000u);
  EXPECT_EQ(b->iterations(make_params(100, 65536)), 100'000u);
  EXPECT_EQ(b->iterations(make_params(10'000'001, 65536)), 1u);
}

TEST(BiasConditionalBranchCount, DirectionAssignmentHonorsNtBranchPct) {
  auto dirs = ferret::bias_conditional_branch_count_internal::assign_directions(/*branches=*/1024,
                                                                                  /*nt_branch_pct=*/50, /*seed=*/1);
  ASSERT_EQ(dirs.size(), 1024u);
  size_t nt = 0;
  for (uint8_t d : dirs) {
    EXPECT_TRUE(d == 0u || d == 1u);
    nt += d;
  }
  EXPECT_EQ(nt, 512u);
}

TEST(BiasConditionalBranchCount, DirectionAssignmentDeterministicForSameSeed) {
  auto a = ferret::bias_conditional_branch_count_internal::assign_directions(64, 50, 42);
  auto b = ferret::bias_conditional_branch_count_internal::assign_directions(64, 50, 42);
  EXPECT_EQ(a, b);
}

TEST(BiasConditionalBranchCount, DirectionAssignmentDiffersBetweenSeeds) {
  auto a = ferret::bias_conditional_branch_count_internal::assign_directions(64, 50, 42);
  auto b = ferret::bias_conditional_branch_count_internal::assign_directions(64, 50, 43);
  EXPECT_NE(a, b);
}

TEST(BiasConditionalBranchCount, DirectionAssignmentAllTPreferredWhenZero) {
  auto dirs = ferret::bias_conditional_branch_count_internal::assign_directions(128, 0, 1);
  for (uint8_t d : dirs) EXPECT_EQ(d, 0u);
}

TEST(BiasConditionalBranchCount, DirectionAssignmentAllNtPreferredWhenHundred) {
  auto dirs = ferret::bias_conditional_branch_count_internal::assign_directions(128, 100, 1);
  for (uint8_t d : dirs) EXPECT_EQ(d, 1u);
}

TEST(BiasConditionalBranchCount, FillIsZeroOrOnePerCell) {
  auto v = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(
      /*branches=*/8, /*pattern_period=*/32, /*bias_pct=*/95, /*nt_branch_pct=*/50, /*seed=*/7);
  ASSERT_EQ(v.size(), 256u);
  for (uint32_t x : v) {
    EXPECT_TRUE(x == 0u || x == 1u);
  }
}

TEST(BiasConditionalBranchCount, FillDeterministicForSameSeed) {
  auto a = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(8, 32, 95, 50, 7);
  auto b = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(8, 32, 95, 50, 7);
  EXPECT_EQ(a, b);
}

TEST(BiasConditionalBranchCount, FillDiffersBetweenSeeds) {
  auto a = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(8, 32, 95, 50, 7);
  auto b = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(8, 32, 95, 50, 8);
  EXPECT_NE(a, b);
}

TEST(BiasConditionalBranchCount, FillDiffersByBiasPct) {
  auto a = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(8, 256, 95, 50, 1);
  auto b = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(8, 256, 50, 50, 1);
  EXPECT_NE(a, b);
}

TEST(BiasConditionalBranchCount, FillFrequencyTracksBiasOnTPreferred) {
  // Spec §10.1: 3-sigma bounds at p=0.95, n=1024.
  auto v = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(
      /*branches=*/64, /*pattern_period=*/1024, /*bias_pct=*/95, /*nt_branch_pct=*/0, /*seed=*/11);
  size_t ones = 0;
  for (size_t t = 0; t < 1024; ++t) ones += v[t * 64 + 0];
  double freq = static_cast<double>(ones) / 1024.0;
  EXPECT_GT(freq, 0.93);
  EXPECT_LT(freq, 0.97);
}

TEST(BiasConditionalBranchCount, FillFrequencyMirrorsBiasOnNtPreferred) {
  auto v = ferret::bias_conditional_branch_count_internal::generate_pattern_fill(
      /*branches=*/64, /*pattern_period=*/1024, /*bias_pct=*/95, /*nt_branch_pct=*/100, /*seed=*/11);
  size_t ones = 0;
  for (size_t t = 0; t < 1024; ++t) ones += v[t * 64 + 0];
  double freq = static_cast<double>(ones) / 1024.0;
  EXPECT_GT(freq, 0.03);
  EXPECT_LT(freq, 0.07);
}

TEST(BiasConditionalBranchCount, RejectsZeroBranches) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(0, 8192)), std::invalid_argument);
}

TEST(BiasConditionalBranchCount, RejectsZeroTotalOutcomes) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 0)), std::invalid_argument);
}

TEST(BiasConditionalBranchCount, RejectsTotalOutcomesBelowBranches) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(64, 32)), std::invalid_argument);
}

TEST(BiasConditionalBranchCount, RejectsBiasPctOutOfRange) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 8192, /*bias=*/-1)), std::invalid_argument);
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 8192, /*bias=*/101)), std::invalid_argument);
}

TEST(BiasConditionalBranchCount, RejectsNtBranchPctOutOfRange) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 8192, 95, /*nt=*/-1)), std::invalid_argument);
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 8192, 95, /*nt=*/101)), std::invalid_argument);
}

TEST(BiasConditionalBranchCount, RejectsSpacingBytesTooSmall) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 8192, 95, 50, /*spacing=*/4)), std::invalid_argument);
}

TEST(BiasConditionalBranchCount, EmitsValidKernelForSmallParams) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*branches=*/4, /*total_outcomes=*/16, /*bias=*/95, /*nt=*/50, /*spacing=*/16);
  ASSERT_NO_THROW(b->emit_kernel(ch.c, p));
  ASSERT_EQ(sljit_get_compiler_error(ch.c), SLJIT_SUCCESS);

  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  ASSERT_NO_THROW(b->verify_layout(ch.c));
  sljit_free_code(code, nullptr);
}

TEST(BiasConditionalBranchCount, EmitsAcceptsEdgeCaseTotalOutcomesEqualsBranches) {
  // total_outcomes == branches → pattern_period == 1.
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*branches=*/4, /*total_outcomes=*/4);
  ASSERT_NO_THROW(b->emit_kernel(ch.c, p));
  ASSERT_EQ(sljit_get_compiler_error(ch.c), SLJIT_SUCCESS);
  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  sljit_free_code(code, nullptr);
}

TEST(BiasConditionalBranchCount, LayoutSnapshotMeetsMinimumSpacing) {
  auto b = ferret::BenchmarkRegistry::create("bias_conditional_branch_count");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  b->emit_kernel(ch.c, make_params(/*branches=*/4, /*total_outcomes=*/16, /*bias=*/95, /*nt=*/50, /*spacing=*/16));
  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  b->verify_layout(ch.c);

  auto snap = ferret::bias_conditional_branch_count_internal::last_layout_snapshot();
  ASSERT_EQ(snap.branches, 4u);
  ASSERT_EQ(snap.spacing, 16u);
  ASSERT_EQ(snap.labels.size(), 5u);
  sljit_uw base = sljit_get_label_addr(snap.labels[0]);
  for (size_t i = 1; i <= snap.branches; ++i) {
    sljit_uw addr = sljit_get_label_addr(snap.labels[i]);
    EXPECT_GE(addr - base, i * snap.spacing) << "site " << i << " closer than min spacing";
  }
  sljit_free_code(code, nullptr);
}

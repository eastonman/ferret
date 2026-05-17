#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

#include "ferret/benchmark.hpp"

extern "C" {
#include <sljitLir.h>
}

namespace ferret::branch_history_footprint_internal {
// Exposed for unit testing; defined in benchmarks/branch_history_footprint.cpp.
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t history_len, int64_t pattern, uint64_t seed);
struct LayoutSnapshot {
  std::vector<sljit_label*> labels;
  size_t branches;
  size_t spacing;
};
LayoutSnapshot last_layout_snapshot();
}  // namespace ferret::branch_history_footprint_internal

TEST(BranchHistoryFootprint, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "branch_history_footprint");
}

namespace {
ferret::Params make_params(int64_t branches, int64_t history_len, int64_t pattern = 1, int64_t spacing = 16) {
  ferret::Params p;
  p.set("branches", branches);
  p.set("history_len", history_len);
  p.set("pattern", pattern);
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

TEST(BranchHistoryFootprint, ExposesBranchesAndHistoryLenAxes) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  ASSERT_EQ(axes.size(), 2u);
  EXPECT_EQ(axes[0].name(), "branches");
  EXPECT_EQ(axes[1].name(), "history_len");
}

TEST(BranchHistoryFootprint, BranchesAxisExpansionMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[0].expand();
  // 1..512 with k=1: {1,2,4,8,16,32,64,128,256,512} = 10 points.
  EXPECT_EQ(vs.size(), 10u);
  EXPECT_EQ(vs.front(), 1);
  EXPECT_EQ(vs.back(), 512);
}

TEST(BranchHistoryFootprint, HistoryLenAxisExpansionMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[1].expand();
  // 4..4096 with k=1: {4,8,16,32,64,128,256,512,1024,2048,4096} = 11 points.
  EXPECT_EQ(vs.size(), 11u);
  EXPECT_EQ(vs.front(), 4);
  EXPECT_EQ(vs.back(), 4096);
}

TEST(BranchHistoryFootprint, SitesPerKernelEqualsBranches) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->sites_per_kernel(make_params(32, 64)), 32u);
  EXPECT_EQ(b->sites_per_kernel(make_params(1, 4)), 1u);
}

TEST(BranchHistoryFootprint, IterationsAmortizesAtTenMillionSites) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->iterations(make_params(1, 4)), 10'000'000u);
  EXPECT_EQ(b->iterations(make_params(100, 4)), 100'000u);
  EXPECT_EQ(b->iterations(make_params(10'000'001, 4)), 1u);  // clamped at >=1
}

TEST(BranchHistoryFootprint, ExposesPatternAndSpacingBytesOptions) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 2u);

  const auto* pattern = find_option(opts, "pattern");
  ASSERT_NE(pattern, nullptr);
  EXPECT_EQ(pattern->default_value, 1);  // random

  const auto* spacing = find_option(opts, "spacing_bytes");
  ASSERT_NE(spacing, nullptr);
  EXPECT_EQ(spacing->default_value, 16);
}

TEST(BranchHistoryFootprint, RejectsSpacingBytesTooSmall) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  // Min site is 8 bytes on AArch64, 6 on x86_64. 4 is below both.
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 4, /*pattern=*/1, /*spacing=*/4)), std::invalid_argument);
}

TEST(BranchHistoryFootprint, RejectsZeroBranches) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(0, 4)), std::invalid_argument);
}

TEST(BranchHistoryFootprint, RejectsZeroHistoryLen) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 0)), std::invalid_argument);
}

TEST(BranchHistoryFootprint, RejectsInvalidPattern) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 4, /*pattern=*/2)), std::invalid_argument);
}

TEST(BranchHistoryFootprint, ZeroPatternProducesAllZeros) {
  auto v = ferret::branch_history_footprint_internal::generate_pattern_fill(
      /*branches=*/4, /*history_len=*/8, /*pattern=*/0, /*seed=*/1);
  ASSERT_EQ(v.size(), 32u);
  for (uint32_t x : v) EXPECT_EQ(x, 0u);
}

TEST(BranchHistoryFootprint, RandomPatternIsDeterministicForSameSeed) {
  auto a = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 1, 42);
  auto b = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 1, 42);
  EXPECT_EQ(a, b);
}

TEST(BranchHistoryFootprint, RandomPatternDiffersBetweenSeeds) {
  auto a = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 1, 42);
  auto b = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 1, 43);
  EXPECT_NE(a, b);
}

TEST(BranchHistoryFootprint, RandomPatternDiffersByParamPoint) {
  // Seed mix includes (branches, history_len), so different points diverge.
  auto a = ferret::branch_history_footprint_internal::generate_pattern_fill(4, 16, 1, 1);
  auto b = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 16, 1, 1);
  // Different sizes → not directly comparable; check that the prefix differs.
  size_t common = std::min(a.size(), b.size());
  bool any_diff = false;
  for (size_t i = 0; i < common; ++i) {
    if (a[i] != b[i]) {
      any_diff = true;
      break;
    }
  }
  EXPECT_TRUE(any_diff);
}

TEST(BranchHistoryFootprint, RandomPatternValuesAreZeroOrOne) {
  auto v = ferret::branch_history_footprint_internal::generate_pattern_fill(8, 32, 1, 7);
  for (uint32_t x : v) {
    EXPECT_TRUE(x == 0u || x == 1u) << "value out of {0,1}: " << x;
  }
}

TEST(BranchHistoryFootprint, EmitsValidKernelForSmallParams) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*branches=*/4, /*history_len=*/8,
                       /*pattern=*/1, /*spacing=*/16);
  ASSERT_NO_THROW(b->emit_kernel(ch.c, p));
  ASSERT_EQ(sljit_get_compiler_error(ch.c), SLJIT_SUCCESS);

  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);

  ASSERT_NO_THROW(b->verify_layout(ch.c));
  sljit_free_code(code, nullptr);
}

TEST(BranchHistoryFootprint, LayoutSnapshotMeetsMinimumSpacing) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  b->emit_kernel(ch.c, make_params(/*branches=*/4, /*history_len=*/4,
                                   /*pattern=*/0, /*spacing=*/16));
  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  b->verify_layout(ch.c);

  auto snap = ferret::branch_history_footprint_internal::last_layout_snapshot();
  ASSERT_EQ(snap.branches, 4u);
  ASSERT_EQ(snap.spacing, 16u);
  ASSERT_EQ(snap.labels.size(), 5u);  // branches + 1 (chain exit)
  sljit_uw base = sljit_get_label_addr(snap.labels[0]);
  for (size_t i = 1; i <= snap.branches; ++i) {
    sljit_uw addr = sljit_get_label_addr(snap.labels[i]);
    EXPECT_GE(addr - base, i * snap.spacing) << "site " << i << " closer than min spacing";
  }
  sljit_free_code(code, nullptr);
}

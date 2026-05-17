#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ferret/benchmark.hpp"

extern "C" {
#include <sljitLir.h>
}

TEST(BranchHistoryFootprint, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "branch_history_footprint");
}

namespace {
ferret::Params make_params(int64_t branches, int64_t history_len,
                           int64_t pattern = 1, int64_t spacing = 16) {
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
  // min site is 8 bytes on AArch64, 9 on x86_64. 4 is below both.
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 4, /*pattern=*/1, /*spacing=*/4)),
               std::invalid_argument);
}

#if defined(__aarch64__) || defined(_M_ARM64)
TEST(BranchHistoryFootprint, RejectsSpacingBytesMisaligned) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  // AArch64 needs 4-byte alignment. 9 is not divisible by 4 and is also >= kMinSiteBytes=8,
  // so this exercises the alignment path specifically.
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 4, 1, /*spacing=*/9)),
               std::invalid_argument);
}
#endif

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
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1, 4, /*pattern=*/2)),
               std::invalid_argument);
}

#if defined(__aarch64__) || defined(_M_ARM64)
TEST(BranchHistoryFootprint, RejectsBranchesAboveAArch64LdrImmediateLimit) {
  auto b = ferret::BenchmarkRegistry::create("branch_history_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(/*branches=*/4096, 4)),
               std::invalid_argument);
}
#endif

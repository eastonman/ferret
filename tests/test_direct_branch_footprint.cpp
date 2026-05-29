#include <gtest/gtest.h>

#include <vector>

#include "ferret/benchmark.hpp"

TEST(DirectBranchFootprint, BranchesAxisExpansionMatchesCiSafeDefault) {
  auto b = ferret::BenchmarkRegistry::create("direct_branch_footprint");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[0].expand();
  const std::vector<int64_t> expected{1, 2, 4, 8, 16, 32, 64, 128, 256, 512, 1024};
  EXPECT_EQ(vs, expected);
}

TEST(DirectBranchFootprint, SpacingBytesAxisExpansionMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("direct_branch_footprint");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[1].expand();
  const std::vector<int64_t> expected{16, 32, 64, 128};
  EXPECT_EQ(vs, expected);
}

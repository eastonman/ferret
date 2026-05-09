#include <gtest/gtest.h>

#include "ferret/sweep.hpp"

using namespace ferret;

TEST(SweepExpand, SingleAxisProducesOneRowPerValue) {
  SweepAxes axes = { Axis::values("x", {1, 2, 3}) };
  auto rows = sweep::expand(axes, {});
  ASSERT_EQ(rows.size(), 3u);
  EXPECT_EQ(rows[0].get<int64_t>("x"), 1);
  EXPECT_EQ(rows[1].get<int64_t>("x"), 2);
  EXPECT_EQ(rows[2].get<int64_t>("x"), 3);
}

TEST(SweepExpand, TwoAxesProduceCrossProduct) {
  SweepAxes axes = {
    Axis::values("x", {1, 2}),
    Axis::values("y", {10, 20, 30}),
  };
  auto rows = sweep::expand(axes, {});
  ASSERT_EQ(rows.size(), 6u);
  EXPECT_EQ(rows[0].get<int64_t>("x"), 1);
  EXPECT_EQ(rows[0].get<int64_t>("y"), 10);
  EXPECT_EQ(rows[1].get<int64_t>("x"), 1);
  EXPECT_EQ(rows[1].get<int64_t>("y"), 20);
  EXPECT_EQ(rows[5].get<int64_t>("x"), 2);
  EXPECT_EQ(rows[5].get<int64_t>("y"), 30);
}

TEST(SweepExpand, OverrideReplacesAxisValues) {
  SweepAxes axes = {
    Axis::log2_range("branches", 1, 1024),
    Axis::values("spacing", {16, 32, 64, 128}),
  };
  std::map<std::string, std::vector<int64_t>> overrides = {
    {"branches", {4, 8, 16}},
  };
  auto rows = sweep::expand(axes, overrides);
  EXPECT_EQ(rows.size(), 3u * 4u);
}

TEST(SweepExpand, OverrideForUnknownAxisIsIgnored) {
  SweepAxes axes = { Axis::values("x", {1, 2}) };
  std::map<std::string, std::vector<int64_t>> overrides = {
    {"y", {100}},
  };
  auto rows = sweep::expand(axes, overrides);
  EXPECT_EQ(rows.size(), 2u);
  EXPECT_FALSE(rows[0].has("y"));
}

TEST(SweepExpand, EmptyAxesProduceOneEmptyRow) {
  auto rows = sweep::expand({}, {});
  ASSERT_EQ(rows.size(), 1u);
  EXPECT_EQ(rows[0].size(), 0u);
}

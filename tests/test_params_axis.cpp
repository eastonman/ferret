#include <gtest/gtest.h>

#include "ferret/axis.hpp"
#include "ferret/params.hpp"

using namespace ferret;

TEST(Params, SetAndGet) {
  Params p;
  p.set("branches", 1024);
  EXPECT_EQ(p.get<int64_t>("branches"), 1024);
  EXPECT_EQ(p.get<size_t>("branches"), 1024u);
}

TEST(Params, MissingKeyThrows) {
  Params p;
  EXPECT_THROW((void)p.get<int64_t>("nope"), std::out_of_range);
}

TEST(Params, GetUnsignedRejectsNegative) {
  Params p;
  p.set("x", -1);
  EXPECT_THROW((void)p.get<size_t>("x"), std::invalid_argument);
  EXPECT_THROW((void)p.get<unsigned>("x"), std::invalid_argument);
  // Signed get is fine — no implicit positivity contract.
  EXPECT_EQ(p.get<int64_t>("x"), -1);
  EXPECT_EQ(p.get<int>("x"), -1);
}

TEST(Params, GetUnsignedAllowsZero) {
  Params p;
  p.set("x", 0);
  EXPECT_EQ(p.get<size_t>("x"), 0u);
}

TEST(Params, KeysAreOrderedByInsertion) {
  Params p;
  p.set("a", 1);
  p.set("b", 2);
  p.set("c", 3);
  std::vector<std::string> keys = p.keys();
  ASSERT_EQ(keys.size(), 3u);
  EXPECT_EQ(keys[0], "a");
  EXPECT_EQ(keys[1], "b");
  EXPECT_EQ(keys[2], "c");
}

TEST(Axis, RangeExpandsLinearly) {
  Axis a = Axis::range("x", 1, 4);
  std::vector<int64_t> v = a.expand();
  EXPECT_EQ(v, (std::vector<int64_t>{1, 2, 3, 4}));
}

TEST(Axis, Log2RangeExpandsPowersOfTwo) {
  Axis a = Axis::log2_range("branches", 1, 32);
  std::vector<int64_t> v = a.expand();
  EXPECT_EQ(v, (std::vector<int64_t>{1, 2, 4, 8, 16, 32}));
}

TEST(Axis, Log2RangeStopsAtLargestPow2BelowUpper) {
  Axis a = Axis::log2_range("branches", 1, 30);
  std::vector<int64_t> v = a.expand();
  EXPECT_EQ(v, (std::vector<int64_t>{1, 2, 4, 8, 16}));
}

TEST(Axis, ValuesPreservesOrder) {
  Axis a = Axis::values("spacing_bytes", {16, 32, 64, 128});
  EXPECT_EQ(a.expand(), (std::vector<int64_t>{16, 32, 64, 128}));
}

TEST(Axis, Log2RangeWithZeroLoThrows) {
  Axis a = Axis::log2_range("branches", 0, 8);
  EXPECT_THROW((void)a.expand(), std::invalid_argument);
}

TEST(Axis, Log2RangeWithNegativeLoThrows) {
  Axis a = Axis::log2_range("branches", -1, 8);
  EXPECT_THROW((void)a.expand(), std::invalid_argument);
}

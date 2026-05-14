#include <gtest/gtest.h>

#include <limits>

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

TEST(Axis, GeomRangeWithKOneEqualsLog2Range) {
  Axis g = Axis::geom_range("branches", 1, 32768, 1);
  Axis l = Axis::log2_range("branches", 1, 32768);
  EXPECT_EQ(g.expand(), l.expand());
}

TEST(Axis, GeomRangeFourSamplesAcrossThreeOctaves) {
  // round(1 * 2^(i/4)) for i = 0..12, dedup adjacent duplicates, with
  // hi=8 already on the natural sequence so no hi-forcing fires.
  Axis a = Axis::geom_range("branches", 1, 8, 4);
  EXPECT_EQ(a.expand(), (std::vector<int64_t>{1, 2, 3, 4, 5, 6, 7, 8}));
}

TEST(Axis, GeomRangeFourSamplesInOneOctave) {
  Axis a = Axis::geom_range("branches", 1024, 2048, 4);
  EXPECT_EQ(a.expand(), (std::vector<int64_t>{1024, 1218, 1448, 1722, 2048}));
}

TEST(Axis, GeomRangeForcesHiAsFinalPoint) {
  // {1, 2, 4, 8} is the natural k=1 expansion up to 10; hi=10 is
  // appended because the last natural point (8) is strictly less.
  Axis a = Axis::geom_range("branches", 1, 10, 1);
  EXPECT_EQ(a.expand(), (std::vector<int64_t>{1, 2, 4, 8, 10}));
}

TEST(Axis, GeomRangeSinglePointWhenLoEqualsHi) {
  Axis a = Axis::geom_range("branches", 5, 5, 4);
  EXPECT_EQ(a.expand(), (std::vector<int64_t>{5}));
}

TEST(Axis, GeomRangeWithZeroLoThrows) {
  Axis a = Axis::geom_range("branches", 0, 8, 1);
  EXPECT_THROW((void)a.expand(), std::invalid_argument);
}

TEST(Axis, GeomRangeWithNegativeLoThrows) {
  Axis a = Axis::geom_range("branches", -1, 8, 1);
  EXPECT_THROW((void)a.expand(), std::invalid_argument);
}

TEST(Axis, GeomRangeWithHiBelowLoThrows) {
  Axis a = Axis::geom_range("branches", 8, 4, 1);
  EXPECT_THROW((void)a.expand(), std::invalid_argument);
}

TEST(Axis, GeomRangeWithNonPositiveKThrows) {
  Axis a0 = Axis::geom_range("branches", 1, 8, 0);
  EXPECT_THROW((void)a0.expand(), std::invalid_argument);
  Axis aneg = Axis::geom_range("branches", 1, 8, -2);
  EXPECT_THROW((void)aneg.expand(), std::invalid_argument);
}

TEST(Axis, GeomRangeSamplesPerOctaveAccessor) {
  EXPECT_EQ(Axis::geom_range("x", 1, 8, 4).samples_per_octave(), 4);
  // Non-geom_range axes report 1 so callers have a sane default.
  EXPECT_EQ(Axis::log2_range("x", 1, 8).samples_per_octave(), 1);
  EXPECT_EQ(Axis::range("x", 0, 10).samples_per_octave(), 1);
  EXPECT_EQ(Axis::values("x", {1, 2, 3}).samples_per_octave(), 1);
}

TEST(Axis, GeomRangeNearInt64MaxExitsCleanly) {
  // lo=2^53, hi=INT64_MAX, k=1: at i=10 the floating multiplication
  // hits exactly 2^63, which is representable as a double but equals
  // (double)INT64_MAX. The overflow guard must use `>=`, not `>`, or
  // else std::llround(2^63) produces UB and a negative sentinel value
  // ends up in the result vector.
  constexpr int64_t kLo = static_cast<int64_t>(1) << 53;
  constexpr int64_t kHi = std::numeric_limits<int64_t>::max();
  Axis a = Axis::geom_range("x", kLo, kHi, 1);
  std::vector<int64_t> v = a.expand();
  ASSERT_FALSE(v.empty());
  EXPECT_EQ(v.front(), kLo);
  EXPECT_EQ(v.back(), kHi);
  // No negative or out-of-range values snuck through the guard.
  for (int64_t x : v) {
    EXPECT_GE(x, kLo);
    EXPECT_LE(x, kHi);
  }
}

#include <gtest/gtest.h>

#include <algorithm>
#include <set>
#include <vector>

#include "ferret/permute.hpp"

using ferret::sattolo_cycle;

namespace {

bool is_permutation(const std::vector<size_t>& v) {
  std::set<size_t> s(v.begin(), v.end());
  if (s.size() != v.size()) return false;
  for (size_t i = 0; i < v.size(); ++i) {
    if (s.find(i) == s.end()) return false;
  }
  return true;
}

bool is_single_cycle(const std::vector<size_t>& next) {
  if (next.empty()) return true;
  size_t n = next.size();
  size_t i = 0;
  for (size_t steps = 0; steps < n; ++steps) {
    i = next[i];
  }
  // Single n-cycle: n steps from 0 must return to 0, with no earlier return.
  if (i != 0) return false;
  i = next[0];
  for (size_t steps = 1; steps < n; ++steps) {
    if (i == 0) return false;
    i = next[i];
  }
  return true;
}

}  // namespace

TEST(Permute, ZeroLengthReturnsEmpty) { EXPECT_TRUE(sattolo_cycle(0, 0).empty()); }

TEST(Permute, SingleElementReturnsIdentity) {
  auto v = sattolo_cycle(1, 0);
  ASSERT_EQ(v.size(), 1u);
  EXPECT_EQ(v[0], 0u);
}

TEST(Permute, IsPermutationForVariousSizes) {
  for (size_t n : {2u, 3u, 4u, 5u, 8u, 16u, 64u, 1024u}) {
    auto v = sattolo_cycle(n, 12345);
    EXPECT_EQ(v.size(), n);
    EXPECT_TRUE(is_permutation(v)) << "n=" << n;
  }
}

TEST(Permute, IsSingleCycleForVariousSizes) {
  for (size_t n : {2u, 3u, 4u, 5u, 8u, 16u, 64u, 1024u}) {
    auto v = sattolo_cycle(n, 7);
    EXPECT_TRUE(is_single_cycle(v)) << "n=" << n;
  }
}

TEST(Permute, IsDeterministicForFixedSeed) {
  auto a = sattolo_cycle(128, 42);
  auto b = sattolo_cycle(128, 42);
  EXPECT_EQ(a, b);
}

TEST(Permute, DifferentSeedsGiveDifferentPermutations) {
  auto a = sattolo_cycle(128, 1);
  auto b = sattolo_cycle(128, 2);
  EXPECT_NE(a, b);
}

TEST(Permute, NoFixedPoint) {
  // Sattolo (single cycle of length n>=2) has no fixed point.
  for (size_t n : {2u, 3u, 8u, 64u, 1024u}) {
    auto v = sattolo_cycle(n, 99);
    for (size_t i = 0; i < n; ++i) {
      EXPECT_NE(v[i], i) << "n=" << n << " i=" << i;
    }
  }
}

TEST(Permute, MixSeedDiffersForDifferentTuples) {
  uint64_t a = ferret::mix_seed(42, 100, 200);
  uint64_t b = ferret::mix_seed(42, 100, 201);
  uint64_t c = ferret::mix_seed(42, 101, 200);
  uint64_t d = ferret::mix_seed(43, 100, 200);
  EXPECT_NE(a, b);
  EXPECT_NE(a, c);
  EXPECT_NE(a, d);
  EXPECT_NE(b, c);
}

TEST(Permute, MixSeedDeterministic) { EXPECT_EQ(ferret::mix_seed(42, 100, 200), ferret::mix_seed(42, 100, 200)); }

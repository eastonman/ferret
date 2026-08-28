#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

extern "C" {
#include <sljitLir.h>
}

#include "ferret/benchmark.hpp"
#include "ferret/jit.hpp"
#include "sljit_test_helpers.hpp"

using ferret::testing::CompilerHandle;
using ferret::testing::find_option;

namespace {

ferret::Params make_params(int64_t separation, int64_t filler = 0, int64_t alu_ops = 16) {
  ferret::Params p;
  p.set("separation", separation);
  p.set("filler", filler);
  p.set("alu_ops", alu_ops);
  p.set("seed", 1);
  return p;
}

// Mirrors kBodyInsns in benchmarks/store_load_distance.cpp.
constexpr size_t kBodyInsns = 4096;

}  // namespace

TEST(StoreLoadDistance, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("store_load_distance");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "store_load_distance");
}

TEST(StoreLoadDistance, ExposesTwoAxesInCsvColumnOrder) {
  auto b = ferret::BenchmarkRegistry::create("store_load_distance");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  ASSERT_EQ(axes.size(), 2u);
  EXPECT_EQ(axes[0].name(), "separation");
  EXPECT_EQ(axes[1].name(), "filler");
}

// Linear, not geometric: the structure under test is a handful of
// instructions wide, so a log2 sweep would step straight over it.
TEST(StoreLoadDistanceAxis, SeparationIsAContiguousRange) {
  auto b = ferret::BenchmarkRegistry::create("store_load_distance");
  ASSERT_NE(b, nullptr);
  auto vs = b->axes()[0].expand();
  ASSERT_EQ(vs.size(), 17u);
  EXPECT_EQ(vs.front(), 0);
  EXPECT_EQ(vs.back(), 16);
  for (size_t i = 1; i < vs.size(); ++i) {
    EXPECT_EQ(vs[i], vs[i - 1] + 1) << "separation axis must not skip values";
  }
}

TEST(StoreLoadDistanceAxis, FillerCoversEveryKind) {
  auto b = ferret::BenchmarkRegistry::create("store_load_distance");
  ASSERT_NE(b, nullptr);
  const std::vector<int64_t> expected{0, 1, 2, 3, 4, 5};
  EXPECT_EQ(b->axes()[1].expand(), expected);
}

TEST(StoreLoadDistance, ExposesAluOpsOptionWithALatencyShadow) {
  auto b = ferret::BenchmarkRegistry::create("store_load_distance");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 1u);
  const auto* alu = find_option(opts, "alu_ops");
  ASSERT_NE(alu, nullptr);
  // Large enough that the filler work hides in the chain's latency
  // instead of turning the row into an issue-width measurement.
  EXPECT_EQ(alu->default_value, 16);
}

// The loop body is sized to a constant instruction count, so sweeping
// separation must not also sweep the L1I footprint. Links shrink as each
// one gets longer.
TEST(StoreLoadDistance, LoopBodyInstructionCountIsHeldRoughlyConstant) {
  auto b = ferret::BenchmarkRegistry::create("store_load_distance");
  ASSERT_NE(b, nullptr);
  for (int64_t sep : {0, 1, 4, 16}) {
    size_t links = b->sites_per_kernel(make_params(sep));
    size_t insns = links * (2 + static_cast<size_t>(sep) + 16);
    EXPECT_LE(insns, kBodyInsns) << "separation=" << sep;
    EXPECT_GT(insns, kBodyInsns / 2) << "separation=" << sep;
  }
}

TEST(StoreLoadDistance, IterationsIsPositiveAcrossTheSweep) {
  auto b = ferret::BenchmarkRegistry::create("store_load_distance");
  ASSERT_NE(b, nullptr);
  for (int64_t sep : {0, 1, 8, 16}) {
    EXPECT_GT(b->iterations(make_params(sep)), 0u) << "separation=" << sep;
  }
}

// ---- Parameter rejection ----

TEST(StoreLoadDistance, RejectsNegativeSeparation) {
  auto b = ferret::BenchmarkRegistry::create("store_load_distance");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(-1)), std::invalid_argument);
}

TEST(StoreLoadDistance, RejectsUnknownFillerKind) {
  auto b = ferret::BenchmarkRegistry::create("store_load_distance");
  ASSERT_NE(b, nullptr);
  for (int64_t bad : {-1, 6}) {
    CompilerHandle ch;
    EXPECT_THROW(b->emit_kernel(ch.c, make_params(2, bad)), std::invalid_argument) << "filler=" << bad;
  }
}

TEST(StoreLoadDistance, RejectsAluOpsBelowOne) {
  auto b = ferret::BenchmarkRegistry::create("store_load_distance");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(2, 0, /*alu_ops=*/0)), std::invalid_argument);
}

// Filler stores and loads walk one 128-byte line per position, so a very
// large separation would push them past the single-instruction
// addressing window and silently add an address computation.
TEST(StoreLoadDistance, RejectsSeparationPastTheFillerAddressingWindow) {
  auto b = ferret::BenchmarkRegistry::create("store_load_distance");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(1024, /*filler=*/2)), std::invalid_argument);
}

// ---- Codegen ----

// verify_layout is the oracle: it throws unless every link is exactly
// (2 + separation + alu_ops) instructions, which is what proves each
// filler kind costs one instruction and no offset needed re-basing.
TEST(StoreLoadDistance, EveryFillerKindEmitsUniformLinks) {
  for (int64_t filler = 0; filler <= 5; ++filler) {
    for (int64_t sep : {0, 1, 8, 16}) {
      auto b = ferret::BenchmarkRegistry::create("store_load_distance");
      ASSERT_NE(b, nullptr);
      ferret::JittedKernel k(*b, make_params(sep, filler));
      EXPECT_TRUE(k.ok()) << "filler=" << filler << " separation=" << sep;
    }
  }
}

TEST(StoreLoadDistance, EmittedKernelRuns) {
  auto b = ferret::BenchmarkRegistry::create("store_load_distance");
  ASSERT_NE(b, nullptr);
  ferret::JittedKernel k(*b, make_params(2, 0, /*alu_ops=*/1));
  ASSERT_TRUE(k.ok());
  k.fn()();
}

// Note: there is deliberately no code-size test here. verify_layout
// already asserts, with strict equality on per-link labels, that a link
// is exactly (2 + separation + alu_ops) instructions -- which is a
// stronger statement than any code-size delta, and it is exercised for
// every filler kind by EveryFillerKindEmitsUniformLinks above.

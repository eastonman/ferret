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

ferret::Params make_params(int64_t load_offset, int64_t store_width = 8, int64_t load_width = 8, int64_t alu_ops = 1) {
  ferret::Params p;
  p.set("load_offset_bytes", load_offset);
  p.set("store_width", store_width);
  p.set("load_width", load_width);
  p.set("alu_ops", alu_ops);
  p.set("seed", 1);
  return p;
}

}  // namespace

TEST(StoreLoadOverlap, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("store_load_overlap");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "store_load_overlap");
}

TEST(StoreLoadOverlap, ExposesThreeAxesInCsvColumnOrder) {
  auto b = ferret::BenchmarkRegistry::create("store_load_overlap");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  ASSERT_EQ(axes.size(), 3u);
  EXPECT_EQ(axes[0].name(), "load_offset_bytes");
  EXPECT_EQ(axes[1].name(), "store_width");
  EXPECT_EQ(axes[2].name(), "load_width");
}

TEST(StoreLoadOverlap, AxisExpansionsMatchSpec) {
  auto b = ferret::BenchmarkRegistry::create("store_load_overlap");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->axes()[0].expand(), (std::vector<int64_t>{-2, -1, 0}));
  EXPECT_EQ(b->axes()[1].expand(), (std::vector<int64_t>{4, 8}));
  EXPECT_EQ(b->axes()[2].expand(), (std::vector<int64_t>{4, 8}));
}

// A rejected point aborts the whole sweep rather than being skipped, so
// the default rectangle has to be valid at every combination.
TEST(StoreLoadOverlap, EveryDefaultAxisCombinationIsAccepted) {
  auto b = ferret::BenchmarkRegistry::create("store_load_overlap");
  ASSERT_NE(b, nullptr);
  for (int64_t off : b->axes()[0].expand()) {
    for (int64_t sw : b->axes()[1].expand()) {
      for (int64_t lw : b->axes()[2].expand()) {
        CompilerHandle ch;
        EXPECT_NO_THROW(b->emit_kernel(ch.c, make_params(off, sw, lw)))
            << "offset=" << off << " store_width=" << sw << " load_width=" << lw;
      }
    }
  }
}

TEST(StoreLoadOverlap, ExposesAluOpsOption) {
  auto b = ferret::BenchmarkRegistry::create("store_load_overlap");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 1u);
  const auto* alu = find_option(opts, "alu_ops");
  ASSERT_NE(alu, nullptr);
  EXPECT_EQ(alu->default_value, 1);
}

// ---- Parameter rejection ----

TEST(StoreLoadOverlap, RejectsWidthsOtherThanFourOrEight) {
  auto b = ferret::BenchmarkRegistry::create("store_load_overlap");
  ASSERT_NE(b, nullptr);
  for (int64_t w : {1, 2, 16}) {
    {
      CompilerHandle ch;
      EXPECT_THROW(b->emit_kernel(ch.c, make_params(0, w, 8)), std::invalid_argument) << "store_width=" << w;
    }
    {
      CompilerHandle ch;
      EXPECT_THROW(b->emit_kernel(ch.c, make_params(0, 8, w)), std::invalid_argument) << "load_width=" << w;
    }
  }
}

// The chain is carried by adding 1, so only byte 0 of the stored value
// is guaranteed to change every link. A load that misses byte 0 reads an
// effectively constant value, the dependency dies, and the row would
// report issue throughput -- which looks FAST and means nothing. Reject
// rather than measure.
TEST(StoreLoadOverlap, RejectsOffsetsThatWouldKillTheDependentChain) {
  auto b = ferret::BenchmarkRegistry::create("store_load_overlap");
  ASSERT_NE(b, nullptr);
  // Positive offset: the load starts above byte 0.
  {
    CompilerHandle ch;
    EXPECT_THROW(b->emit_kernel(ch.c, make_params(1)), std::invalid_argument);
  }
  // Negative beyond the load's own width: the load ends at or below byte 0.
  {
    CompilerHandle ch;
    EXPECT_THROW(b->emit_kernel(ch.c, make_params(-8, 8, 8)), std::invalid_argument);
  }
  {
    CompilerHandle ch;
    EXPECT_THROW(b->emit_kernel(ch.c, make_params(-4, 8, 4)), std::invalid_argument);
  }
}

TEST(StoreLoadOverlap, RejectsAluOpsBelowOne) {
  auto b = ferret::BenchmarkRegistry::create("store_load_overlap");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(0, 8, 8, /*alu_ops=*/0)), std::invalid_argument);
}

// ---- Codegen ----

// An unaligned offset can push sljit past the scaled-immediate form. The
// kernel addresses through a register pointing AT the slot so offsets
// stay inside the 256-byte unscaled window; verify_layout proves it.
TEST(StoreLoadOverlap, EveryDefaultCombinationEmitsUniformLinks) {
  auto axes = ferret::BenchmarkRegistry::create("store_load_overlap")->axes();
  for (int64_t off : axes[0].expand()) {
    for (int64_t sw : axes[1].expand()) {
      for (int64_t lw : axes[2].expand()) {
        auto b = ferret::BenchmarkRegistry::create("store_load_overlap");
        ASSERT_NE(b, nullptr);
        ferret::JittedKernel k(*b, make_params(off, sw, lw));
        EXPECT_TRUE(k.ok()) << "offset=" << off << " store_width=" << sw << " load_width=" << lw;
      }
    }
  }
}

TEST(StoreLoadOverlap, EmittedKernelRuns) {
  auto b = ferret::BenchmarkRegistry::create("store_load_overlap");
  ASSERT_NE(b, nullptr);
  ferret::JittedKernel k(*b, make_params(0));
  ASSERT_TRUE(k.ok());
  k.fn()();
}

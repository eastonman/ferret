#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <vector>

extern "C" {
#include <sljitLir.h>
}

#include "ferret/benchmark.hpp"
#include "sljit_test_helpers.hpp"

namespace ferret::nested_call_depth_internal {
// Exposed for unit testing; defined in benchmarks/nested_call_depth.cpp.
std::vector<uint8_t> generate_path_table(size_t rows, size_t row_stride, uint64_t seed);
}  // namespace ferret::nested_call_depth_internal

using ferret::testing::CompilerHandle;
using ferret::testing::find_option;

namespace {

ferret::Params make_params(int64_t depth, int64_t variant, int64_t path_table_rows) {
  ferret::Params p;
  p.set("depth", depth);
  p.set("variant", variant);
  p.set("path_table_rows", path_table_rows);
  p.set("seed", 1);
  return p;
}

// ---- Registry / shape ----

TEST(NestedCallDepth, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "nested_call_depth");
}

TEST(NestedCallDepth, ExposesDepthAndVariantAxes) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  ASSERT_EQ(axes.size(), 2u);

  // Order matters for CSV column ordering; verify both names appear.
  std::vector<std::string> names;
  names.reserve(axes.size());
  for (const auto& a : axes) names.push_back(a.name());
  EXPECT_NE(std::find(names.begin(), names.end(), "depth"), names.end());
  EXPECT_NE(std::find(names.begin(), names.end(), "variant"), names.end());
}

TEST(NestedCallDepth, ExposesPathTableRowsOption) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 1u);
  const auto* rows = find_option(opts, "path_table_rows");
  ASSERT_NE(rows, nullptr);
  EXPECT_EQ(rows->default_value, 256);
}

TEST(NestedCallDepth, VariantAxisDefaultsToSingleValueOne) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  for (const auto& a : b->axes()) {
    if (a.name() == "variant") {
      auto vs = a.expand();
      ASSERT_EQ(vs.size(), 1u);
      EXPECT_EQ(vs[0], 1) << "default variant axis should be {1} — the canonical K=2 kernel";
      return;
    }
  }
  FAIL() << "no variant axis";
}

// ---- Pre-flight validation (depth, variant, path_table_rows) ----

TEST(NestedCallDepth, RejectsZeroDepth) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*depth=*/0, /*variant=*/1, /*path_table_rows=*/256);
  EXPECT_THROW(b->emit_kernel(ch.c, p), std::invalid_argument);
}

TEST(NestedCallDepth, RejectsNegativeVariant) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*depth=*/4, /*variant=*/-1, /*path_table_rows=*/256);
  EXPECT_THROW(b->emit_kernel(ch.c, p), std::invalid_argument);
}

TEST(NestedCallDepth, RejectsVariantOutOfRange) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*depth=*/4, /*variant=*/3, /*path_table_rows=*/256);
  EXPECT_THROW(b->emit_kernel(ch.c, p), std::invalid_argument);
}

TEST(NestedCallDepth, RejectsPathTableRowsNotPowerOfTwoForVariant2) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*depth=*/4, /*variant=*/2, /*path_table_rows=*/3);
  EXPECT_THROW(b->emit_kernel(ch.c, p), std::invalid_argument);
}

TEST(NestedCallDepth, RejectsPathTableRowsLessThanTwoForVariant2) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*depth=*/4, /*variant=*/2, /*path_table_rows=*/1);
  EXPECT_THROW(b->emit_kernel(ch.c, p), std::invalid_argument);
}

TEST(NestedCallDepth, AcceptsBadPathTableRowsForVariant0Or1) {
  // variants 0 and 1 don't use the path table — bad path_table_rows must not
  // be rejected when those variants are selected, otherwise users get
  // spurious failures from leftover CLI defaults.
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  for (int64_t variant : {int64_t{0}, int64_t{1}}) {
    CompilerHandle ch;
    auto p = make_params(/*depth=*/4, variant, /*path_table_rows=*/3);
    EXPECT_NO_THROW(b->emit_kernel(ch.c, p)) << "variant=" << variant;
  }
}

// ---- Path-table generator helper (only used by variant=2 at runtime) ----

TEST(NestedCallDepthPathTable, DimensionsMatchRowsTimesStride) {
  auto t = ferret::nested_call_depth_internal::generate_path_table(
      /*rows=*/16, /*row_stride=*/5, /*seed=*/123);
  EXPECT_EQ(t.size(), 16u * 5u);
}

TEST(NestedCallDepthPathTable, BytesAreInDispatchRange) {
  auto t = ferret::nested_call_depth_internal::generate_path_table(
      /*rows=*/64, /*row_stride=*/10, /*seed=*/0xdeadbeef);
  for (uint8_t b : t) {
    EXPECT_LT(b, 8) << "byte out of [0, 8): " << static_cast<int>(b);
  }
}

TEST(NestedCallDepthPathTable, SameSeedSameTable) {
  auto a = ferret::nested_call_depth_internal::generate_path_table(32, 8, 0x42);
  auto b = ferret::nested_call_depth_internal::generate_path_table(32, 8, 0x42);
  EXPECT_EQ(a, b);
}

TEST(NestedCallDepthPathTable, DifferentSeedDifferentTable) {
  auto a = ferret::nested_call_depth_internal::generate_path_table(32, 8, 0x42);
  auto b = ferret::nested_call_depth_internal::generate_path_table(32, 8, 0x43);
  EXPECT_NE(a, b);
}

}  // namespace

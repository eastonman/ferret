#include <gtest/gtest.h>

#include "ferret/benchmark.hpp"

namespace {

TEST(NestedCallDepth, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "nested_call_depth");
}

TEST(NestedCallDepth, ExposesSingleDepthAxis) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  ASSERT_EQ(axes.size(), 1u);
  EXPECT_EQ(axes[0].name(), "depth");
}

TEST(NestedCallDepth, ExposesPathTableRowsOptionWithDefault256) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 1u);
  EXPECT_EQ(opts[0].name, "path_table_rows");
  EXPECT_EQ(opts[0].default_value, 256);
}

}  // namespace

#include <memory>

extern "C" {
#include <sljitLir.h>
}

namespace {

ferret::Params make_params(int64_t depth, int64_t path_table_rows) {
  ferret::Params p;
  p.set("depth", depth);
  p.set("path_table_rows", path_table_rows);
  p.set("seed", 1);
  return p;
}

struct CompilerHandle {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  ~CompilerHandle() {
    if (c) sljit_free_compiler(c);
  }
};

TEST(NestedCallDepth, RejectsZeroDepth) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*depth=*/0, /*path_table_rows=*/4096);
  EXPECT_THROW(b->emit_kernel(ch.c, p), std::invalid_argument);
}

TEST(NestedCallDepth, RejectsPathTableRowsNotPowerOfTwo) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*depth=*/4, /*path_table_rows=*/3);
  EXPECT_THROW(b->emit_kernel(ch.c, p), std::invalid_argument);
}

TEST(NestedCallDepth, RejectsPathTableRowsLessThanTwo) {
  auto b = ferret::BenchmarkRegistry::create("nested_call_depth");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  auto p = make_params(/*depth=*/4, /*path_table_rows=*/1);
  EXPECT_THROW(b->emit_kernel(ch.c, p), std::invalid_argument);
}

}  // namespace

#include <cstdint>
#include <vector>

namespace ferret::nested_call_depth_internal {
// Exposed for unit testing; defined in benchmarks/nested_call_depth.cpp.
std::vector<uint8_t> generate_path_table(size_t rows, size_t row_stride, uint64_t seed);
}  // namespace ferret::nested_call_depth_internal

namespace {

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

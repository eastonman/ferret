#include <gtest/gtest.h>

#include <algorithm>

#include "ferret/benchmark.hpp"

using namespace ferret;

namespace {
struct DummyBench : Benchmark {
  std::string name() const override { return "dummy"; }
  SweepAxes axes() const override { return {Axis::values("x", {1, 2, 3})}; }
  size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("x"); }
  size_t iterations(const Params&) const override { return 1; }
  void emit_kernel(sljit_compiler*, const Params&) override {}
};
FERRET_BENCHMARK("dummy_for_test", DummyBench);
}  // namespace

TEST(BenchmarkRegistry, RegistersAndCreates) {
  auto b = BenchmarkRegistry::create("dummy_for_test");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->axes().size(), 1u);
}

TEST(BenchmarkRegistry, MissingNameReturnsNull) {
  auto b = BenchmarkRegistry::create("does_not_exist");
  EXPECT_EQ(b, nullptr);
}

TEST(BenchmarkRegistry, NamesIncludesRegistered) {
  auto names = BenchmarkRegistry::names();
  EXPECT_NE(std::find(names.begin(), names.end(), "dummy_for_test"), names.end());
}

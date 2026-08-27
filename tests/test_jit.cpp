#include <gtest/gtest.h>

extern "C" {
#include <sljitLir.h>
}

#include "ferret/benchmark.hpp"
#include "ferret/jit.hpp"
#include "ferret/params.hpp"

using namespace ferret;

namespace {

// Minimal test fixture: emits a function that immediately returns.
struct EmptyBench : Benchmark {
  [[nodiscard]] std::string name() const override { return "test_empty"; }
  [[nodiscard]] SweepAxes axes() const override { return {}; }
  [[nodiscard]] size_t sites_per_kernel(const Params& /*p*/) const override { return 1; }
  [[nodiscard]] size_t iterations(const Params& /*p*/) const override { return 1; }
  void emit_kernel(sljit_compiler* c, const Params& /*p*/) override {
    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 0, 0, 0);
    sljit_emit_return_void(c);
  }
  MeasurementRow measure_row(const Params& p, int reps, int warmup) override {
    return runner::single_kernel_measure(*this, p, reps, warmup);
  }
};

}  // namespace

TEST(JittedKernel, DefaultConstructedIsNotOk) {
  JittedKernel k;
  EXPECT_FALSE(k.ok());
}

TEST(JittedKernel, CompilesEmptyBenchmarkAndIsCallable) {
  EmptyBench b;
  Params p;
  JittedKernel k(b, p);
  ASSERT_TRUE(k.ok());
  // Calling the JIT-emitted void function must not crash.
  k.fn()();
}

// The failed-compile path (ok() == false on sljit error) is already
// covered by tests/test_integration.cpp via benchmarks that trip
// sljit's compile errors. A unit test here would need to construct an
// sljit error state reliably across architectures, which is fragile.

TEST(JittedKernel, MoveConstructTransfersOwnership) {
  EmptyBench b;
  Params p;
  JittedKernel src(b, p);
  ASSERT_TRUE(src.ok());

  JittedKernel dst(std::move(src));
  EXPECT_TRUE(dst.ok());
  EXPECT_FALSE(src.ok());  // moved-from is in not-ok state, dtor must be a no-op
  dst.fn()();
}

TEST(JittedKernel, MoveAssignReleasesPrevious) {
  EmptyBench b;
  Params p;
  JittedKernel a(b, p);
  JittedKernel c(b, p);
  ASSERT_TRUE(a.ok());
  ASSERT_TRUE(c.ok());

  a = std::move(c);  // a's original code must be freed without leak/double-free
  EXPECT_TRUE(a.ok());
  EXPECT_FALSE(c.ok());
}

// Stress: many move/destroy/recreate cycles. Under ASan/MSan a leak or
// double-free in the dtor or move ops would fail this test.
TEST(JittedKernel, StressMoveDestroyRecreate) {
  EmptyBench b;
  Params p;
  for (int i = 0; i < 200; ++i) {
    JittedKernel a(b, p);
    JittedKernel bk(std::move(a));
    JittedKernel ck;
    ck = std::move(bk);
    ASSERT_TRUE(ck.ok());
    ck.fn()();
  }
}

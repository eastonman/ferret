extern "C" {
#include <sljitLir.h>
}

#include <gtest/gtest.h>

#include "ferret/bench_helpers.hpp"
#include "sljit_test_helpers.hpp"

using ferret::testing::CompilerHandle;

namespace {

// Minimal kernel: outer loop that decrements a counter; body increments R0.
// After `iters` iterations, R0 should equal `iters`.
TEST(BenchHelpers, EmitOuterLoopRunsIterTimes) {
  CompilerHandle ch;
  ASSERT_NE(ch.c, nullptr);

  // Return R0 via SLJIT_RETURN.
  sljit_emit_enter(ch.c, 0, SLJIT_ARGS0(W), /*scratches=*/2, /*saved=*/0, /*local_size=*/0);
  sljit_emit_op1(ch.c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);

  ferret::emit_outer_loop(ch.c, SLJIT_R1, /*iters=*/7,
                          [&] { sljit_emit_op2(ch.c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1); });

  sljit_emit_return(ch.c, SLJIT_MOV, SLJIT_R0, 0);

  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);

  using fn_t = sljit_sw (*)();
  auto fn = reinterpret_cast<fn_t>(code);
  EXPECT_EQ(fn(), 7);

  sljit_free_code(code, nullptr);
}

TEST(BenchHelpers, EmitOuterLoopOneIterationStillRunsBody) {
  CompilerHandle ch;
  ASSERT_NE(ch.c, nullptr);

  sljit_emit_enter(ch.c, 0, SLJIT_ARGS0(W), 2, 0, 0);
  sljit_emit_op1(ch.c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);

  ferret::emit_outer_loop(ch.c, SLJIT_R1, /*iters=*/1,
                          [&] { sljit_emit_op2(ch.c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1); });

  sljit_emit_return(ch.c, SLJIT_MOV, SLJIT_R0, 0);

  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  auto fn = reinterpret_cast<sljit_sw (*)()>(code);
  EXPECT_EQ(fn(), 1);

  sljit_free_code(code, nullptr);
}

TEST(BenchHelpers, EmitOuterLoopZeroItersAsserts) {
  CompilerHandle ch;
  ASSERT_NE(ch.c, nullptr);
  sljit_emit_enter(ch.c, 0, SLJIT_ARGS0(W), 2, 0, 0);
  // EXPECT_DEBUG_DEATH (not EXPECT_DEATH): the assert is compiled out in
  // Release builds (NDEBUG defined). DEBUG_DEATH executes the expression
  // once in Release without expecting a crash, and asserts a death match
  // only in Debug builds — the correct test for an assert-based guard.
  EXPECT_DEBUG_DEATH(ferret::emit_outer_loop(ch.c, SLJIT_R1, /*iters=*/0, [] {}), "iters > 0");
}

TEST(BenchHelpers, ComputeIterationsScalesByBudget) {
  EXPECT_EQ(ferret::compute_iterations(10'000'000, 1), 10'000'000U);
  EXPECT_EQ(ferret::compute_iterations(10'000'000, 1000), 10'000U);
  EXPECT_EQ(ferret::compute_iterations(10'000'000, 100'000'000), 1U);
  EXPECT_EQ(ferret::compute_iterations(0, 100), 1U);  // floor at 1
}

TEST(BenchHelpers, VerifyUniformSpacingPassesOnExactSpacing) {
  CompilerHandle ch;
  ASSERT_NE(ch.c, nullptr);
  sljit_emit_enter(ch.c, 0, SLJIT_ARGS0V(), 1, 0, 0);

  std::vector<sljit_label*> labels;
  labels.push_back(sljit_emit_label(ch.c));
  for (int i = 0; i < 4; ++i) {
    ferret::emit_outer_loop(ch.c, SLJIT_R0, 1, [] {});  // placeholder body
    labels.push_back(sljit_emit_label(ch.c));
  }
  sljit_emit_return_void(ch.c);
  void* code = sljit_generate_code(ch.c, 0, nullptr);
  ASSERT_NE(code, nullptr);

  // After generate_code, every label has a real address. We cannot assert
  // the exact spacing because emit_outer_loop's size is non-zero and
  // arch-dependent; but the helper should accept whatever spacing happens
  // to be uniform across the four sites.
  size_t base = sljit_get_label_addr(labels[0]);
  size_t step = sljit_get_label_addr(labels[1]) - base;
  EXPECT_NO_THROW(ferret::verify_uniform_spacing(labels, step, /*strict=*/true));

  sljit_free_code(code, nullptr);
}

TEST(BenchHelpers, VerifyUniformSpacingNoOpsOnEmpty) {
  std::vector<sljit_label*> empty;
  EXPECT_NO_THROW(ferret::verify_uniform_spacing(empty, 16, /*strict=*/true));  // no-op
}

}  // namespace

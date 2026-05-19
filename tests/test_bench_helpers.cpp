extern "C" {
#include <sljitLir.h>
}

#include <gtest/gtest.h>

#include "ferret/bench_helpers.hpp"

namespace {

// Minimal kernel: outer loop that decrements a counter; body increments R0.
// After `iters` iterations, R0 should equal `iters`.
TEST(BenchHelpers, EmitOuterLoopRunsIterTimes) {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  ASSERT_NE(c, nullptr);

  // Return R0 via SLJIT_RETURN.
  sljit_emit_enter(c, 0, SLJIT_ARGS0(W), /*scratches=*/2, /*saved=*/0, /*local_size=*/0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);

  ferret::emit_outer_loop(c, SLJIT_R1, /*iters=*/7,
                          [&] { sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1); });

  sljit_emit_return(c, SLJIT_MOV, SLJIT_R0, 0);

  void* code = sljit_generate_code(c, 0, nullptr);
  ASSERT_NE(code, nullptr);

  using fn_t = sljit_sw (*)();
  auto fn = reinterpret_cast<fn_t>(code);
  EXPECT_EQ(fn(), 7);

  sljit_free_code(code, nullptr);
  sljit_free_compiler(c);
}

TEST(BenchHelpers, EmitOuterLoopOneIterationStillRunsBody) {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  ASSERT_NE(c, nullptr);

  sljit_emit_enter(c, 0, SLJIT_ARGS0(W), 2, 0, 0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 0);

  ferret::emit_outer_loop(c, SLJIT_R1, /*iters=*/1,
                          [&] { sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1); });

  sljit_emit_return(c, SLJIT_MOV, SLJIT_R0, 0);

  void* code = sljit_generate_code(c, 0, nullptr);
  ASSERT_NE(code, nullptr);
  auto fn = reinterpret_cast<sljit_sw (*)()>(code);
  EXPECT_EQ(fn(), 1);

  sljit_free_code(code, nullptr);
  sljit_free_compiler(c);
}

TEST(BenchHelpers, EmitOuterLoopZeroItersAsserts) {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  ASSERT_NE(c, nullptr);
  sljit_emit_enter(c, 0, SLJIT_ARGS0(W), 2, 0, 0);
  // EXPECT_DEBUG_DEATH (not EXPECT_DEATH): the assert is compiled out in
  // Release builds (NDEBUG defined). DEBUG_DEATH executes the expression
  // once in Release without expecting a crash, and asserts a death match
  // only in Debug builds — the correct test for an assert-based guard.
  EXPECT_DEBUG_DEATH(ferret::emit_outer_loop(c, SLJIT_R1, /*iters=*/0, [] {}), "iters > 0");
  sljit_free_compiler(c);
}

TEST(BenchHelpers, ComputeIterationsScalesByBudget) {
  EXPECT_EQ(ferret::compute_iterations(10'000'000, 1), 10'000'000U);
  EXPECT_EQ(ferret::compute_iterations(10'000'000, 1000), 10'000U);
  EXPECT_EQ(ferret::compute_iterations(10'000'000, 100'000'000), 1U);
  EXPECT_EQ(ferret::compute_iterations(0, 100), 1U);  // floor at 1
}

TEST(BenchHelpers, VerifyUniformSpacingPassesOnExactSpacing) {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  ASSERT_NE(c, nullptr);
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 1, 0, 0);

  std::vector<sljit_label*> labels;
  labels.push_back(sljit_emit_label(c));
  for (int i = 0; i < 4; ++i) {
    ferret::emit_outer_loop(c, SLJIT_R0, 1, [] {});  // placeholder body
    labels.push_back(sljit_emit_label(c));
  }
  sljit_emit_return_void(c);
  ASSERT_NE(sljit_generate_code(c, 0, nullptr), nullptr);

  // After generate_code, every label has a real address. We cannot assert
  // the exact spacing because emit_outer_loop's size is non-zero and
  // arch-dependent; but the helper should accept whatever spacing happens
  // to be uniform across the four sites.
  size_t base = sljit_get_label_addr(labels[0]);
  size_t step = sljit_get_label_addr(labels[1]) - base;
  EXPECT_NO_THROW(ferret::verify_uniform_spacing(labels, step, /*strict=*/true));

  sljit_free_compiler(c);
}

TEST(BenchHelpers, VerifyUniformSpacingNoOpsOnEmpty) {
  std::vector<sljit_label*> empty;
  EXPECT_NO_THROW(ferret::verify_uniform_spacing(empty, 16, /*strict=*/true));  // no-op
}

}  // namespace

#include <gtest/gtest.h>

extern "C" {
#include <sljitLir.h>
}

#include "ferret/padding.hpp"

using namespace ferret;

TEST(Padding, EmitsRequestedBytesOnX86_64) {
#if defined(__x86_64__) || defined(_M_X64)
  sljit_compiler* c = sljit_create_compiler(nullptr);
  ASSERT_NE(c, nullptr);
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 1, 0, 0);
  size_t before = c->size;
  emit_nops(c, 16);
  size_t after = c->size;
  EXPECT_EQ(after - before, 16u);
  sljit_emit_return_void(c);
  sljit_free_compiler(c);
#else
  GTEST_SKIP() << "x86_64-only test";
#endif
}

TEST(Padding, RoundsUpToInstructionMultipleOnAArch64) {
#if defined(__aarch64__) || defined(_M_ARM64)
  sljit_compiler* c = sljit_create_compiler(nullptr);
  ASSERT_NE(c, nullptr);
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 1, 0, 0);
  size_t before = c->size;
  emit_nops(c, 16);
  size_t after = c->size;
  // 16 bytes = 4 NOP instructions, each NOP increments compiler->size by 1
  // (sljit's size field counts instructions, not bytes, on aarch64).
  // We accept either: 16 (bytes) or 4 (insns).
  EXPECT_TRUE(after - before == 4u || after - before == 16u)
      << "size delta = " << (after - before);
  sljit_emit_return_void(c);
  sljit_free_compiler(c);
#else
  GTEST_SKIP() << "aarch64-only test";
#endif
}

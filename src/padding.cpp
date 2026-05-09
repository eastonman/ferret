#include "ferret/padding.hpp"

extern "C" {
#include <sljitLir.h>
}

#include <cstdint>

namespace ferret {

void emit_nops(sljit_compiler* c, size_t bytes) {
#if defined(__x86_64__) || defined(_M_X64)
  static constexpr uint8_t nop = 0x90;
  for (size_t i = 0; i < bytes; ++i) {
    sljit_emit_op_custom(c, const_cast<uint8_t*>(&nop), 1);
  }
#elif defined(__aarch64__) || defined(_M_ARM64)
  static const uint32_t nop_insn = 0xd503201f;  // AArch64 NOP
  size_t insns = (bytes + 3) / 4;               // round up
  for (size_t i = 0; i < insns; ++i) {
    sljit_emit_op_custom(c, const_cast<uint32_t*>(&nop_insn), 4);
  }
#else
#error "ferret v1 supports only x86_64 and aarch64"
#endif
}

}  // namespace ferret

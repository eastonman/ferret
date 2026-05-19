#include "ferret/padding.hpp"

extern "C" {
#include <sljitLir.h>
}

#include <cstdint>

namespace ferret {

void emit_nops(sljit_compiler* c, size_t bytes) {
  static const uint32_t nop_insn = 0xd503201f;  // AArch64 NOP
  size_t insns = (bytes + 3) / 4;               // round up
  for (size_t i = 0; i < insns; ++i) {
    sljit_emit_op_custom(c, const_cast<uint32_t*>(&nop_insn), 4);
  }
}

}  // namespace ferret

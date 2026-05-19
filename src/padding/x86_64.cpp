#include "ferret/padding.hpp"

extern "C" {
#include <sljitLir.h>
}

#include <cstdint>

namespace ferret {

void emit_nops(sljit_compiler* c, size_t bytes) {
  static constexpr uint8_t nop = 0x90;
  for (size_t i = 0; i < bytes; ++i) {
    sljit_emit_op_custom(c, const_cast<uint8_t*>(&nop), 1);
  }
}

}  // namespace ferret

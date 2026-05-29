#include "ferret/padding.hpp"

extern "C" {
#include <sljitLir.h>
}

#include <array>
#include <cstdint>

namespace ferret {

void emit_nops(sljit_compiler* c, size_t bytes) {
  static constexpr std::array<uint8_t, 15> nops = {
      0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
  };
  while (bytes >= nops.size()) {
    sljit_emit_op_custom(c, const_cast<uint8_t*>(nops.data()), static_cast<sljit_u32>(nops.size()));
    bytes -= nops.size();
  }
  if (bytes > 0) {
    sljit_emit_op_custom(c, const_cast<uint8_t*>(nops.data()), static_cast<sljit_u32>(bytes));
  }
}

}  // namespace ferret

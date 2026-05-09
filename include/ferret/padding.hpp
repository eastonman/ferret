#pragma once

#include <cstddef>

struct sljit_compiler;

namespace ferret {

// Emits NOPs into the sljit code buffer. On x86_64 emits exactly `bytes`
// 1-byte NOPs (0x90). On AArch64 rounds up to the next multiple of 4 and
// emits that many NOP instructions (0xd503201f).
//
// Used by direct_branch_footprint to pad each branch out to the
// requested per-branch spacing.
void emit_nops(sljit_compiler* c, size_t bytes);

}  // namespace ferret

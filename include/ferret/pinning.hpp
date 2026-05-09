#pragma once

namespace ferret::pinning {

// All best-effort: returns true if the operation succeeded, false if it
// failed for any reason (permission, unsupported, invalid). Never throws,
// never aborts the program. Callers should log failures but continue.

bool pin_to_core(int cpu);
bool boost_priority();
bool lock_memory();

}  // namespace ferret::pinning

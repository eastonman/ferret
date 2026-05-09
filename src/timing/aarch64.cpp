#include "ferret/timing.hpp"

#if defined(__aarch64__) || defined(_M_ARM64)

namespace ferret::timing {

uint64_t arch_now_ticks() {
  uint64_t v;
  asm volatile("mrs %0, cntvct_el0" : "=r"(v));
  return v;
}

}  // namespace ferret::timing

#endif

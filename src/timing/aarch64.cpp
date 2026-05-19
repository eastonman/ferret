#include "ferret/timing.hpp"

namespace ferret::timing {

uint64_t arch_now_ticks() {
  uint64_t v;
  asm volatile("mrs %0, cntvct_el0" : "=r"(v));
  return v;
}

}  // namespace ferret::timing

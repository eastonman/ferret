#include "ferret/timing.hpp"

#include <x86intrin.h>

namespace ferret::timing {

uint64_t arch_now_ticks() {
  unsigned aux;
  return __rdtscp(&aux);
}

}  // namespace ferret::timing

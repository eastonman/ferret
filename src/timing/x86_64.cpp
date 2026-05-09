#include "ferret/timing.hpp"

#if defined(__x86_64__) || defined(_M_X64)

#include <x86intrin.h>

namespace ferret::timing {

uint64_t arch_now_ticks() {
  unsigned aux;
  return __rdtscp(&aux);
}

}  // namespace ferret::timing

#endif

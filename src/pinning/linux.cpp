#include "ferret/pinning.hpp"

#include <sched.h>

namespace ferret::pinning {

bool pin_to_core(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return sched_setaffinity(0, sizeof(set), &set) == 0;
}

}  // namespace ferret::pinning

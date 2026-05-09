#include "ferret/pinning.hpp"

#if defined(__linux__) || defined(__ANDROID__)

#include <pthread.h>
#include <sched.h>
#include <sys/mman.h>
#include <sys/resource.h>

namespace ferret::pinning {

bool pin_to_core(int cpu) {
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(cpu, &set);
  return pthread_setaffinity_np(pthread_self(), sizeof(set), &set) == 0;
}

bool boost_priority() {
  return setpriority(PRIO_PROCESS, 0, -10) == 0;
}

bool lock_memory() {
  return mlockall(MCL_CURRENT | MCL_FUTURE) == 0;
}

}  // namespace ferret::pinning

#endif

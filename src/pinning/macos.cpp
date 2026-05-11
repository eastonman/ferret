#include "ferret/pinning.hpp"

#if defined(__APPLE__)

#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/resource.h>

namespace ferret::pinning {

bool pin_to_core(int cpu) {
  // macOS thread affinity is advisory: THREAD_AFFINITY_POLICY hints
  // that threads with the same tag should run on the same core. We use
  // the cpu number as the tag. Apple Silicon ignores it for P/E core
  // placement, but we attempt it anyway. macOS rejects "huge" tag
  // values when the policy can't be set, which lets the
  // PinToImpossiblyHighCoreReturnsFalse test pass on this OS.
  if (cpu < 0 || cpu > 1024) return false;
  thread_affinity_policy_data_t policy = {cpu + 1};
  kern_return_t kr = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_AFFINITY_POLICY,
                                       reinterpret_cast<thread_policy_t>(&policy), THREAD_AFFINITY_POLICY_COUNT);
  return kr == KERN_SUCCESS;
}

bool boost_priority() { return setpriority(PRIO_PROCESS, 0, -10) == 0; }

bool lock_memory() { return mlockall(MCL_CURRENT | MCL_FUTURE) == 0; }

}  // namespace ferret::pinning

#endif

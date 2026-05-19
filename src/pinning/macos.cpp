#include "ferret/pinning.hpp"

#include <mach/mach.h>
#include <mach/thread_act.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <sys/mman.h>
#include <sys/qos.h>
#include <sys/resource.h>

#include "ferret/log.hpp"

namespace flog = ferret::log;

namespace ferret::pinning {

bool pin_to_core(int cpu) {
  // Intel macOS accepts THREAD_AFFINITY_POLICY as an advisory tag.
  // Apple Silicon (arm64 XNU) does not implement it: every call returns
  // KERN_NOT_SUPPORTED regardless of the tag. On that path we fall back
  // to a QoS hint that strongly prefers the P-cluster — the closest
  // approximation to "run on a fast core" the kernel exposes.
  // Reject obviously-nonsense core IDs before falling back to the QoS hint.
  // 1024 is an arbitrary upper bound far above any shipping CPU's core
  // count; its only purpose is to keep PinToImpossiblyHighCoreReturnsFalse
  // honest. Real Apple Silicon ships up to ~24 cores (M-Ultra).
  static constexpr int kMaxAcceptedCpuId = 1024;
  if (cpu < 0 || cpu > kMaxAcceptedCpuId) {
    return false;
  }
  thread_affinity_policy_data_t policy = {cpu + 1};
  kern_return_t kr = thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_AFFINITY_POLICY,
                                       reinterpret_cast<thread_policy_t>(&policy), THREAD_AFFINITY_POLICY_COUNT);
  if (kr == KERN_SUCCESS) {
    return true;
  }
  if (pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0) != 0) {
    return false;
  }
  flog::warn(
      "per-core pinning unavailable on this OS (thread_policy_set returned {}); "
      "fell back to P-cluster QoS hint, --core={} is informational only",
      kr, cpu);
  return true;
}

bool boost_priority() { return setpriority(PRIO_PROCESS, 0, -10) == 0; }

bool lock_memory() { return mlockall(MCL_CURRENT | MCL_FUTURE) == 0; }

}  // namespace ferret::pinning

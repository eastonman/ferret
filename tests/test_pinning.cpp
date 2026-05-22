#include <gtest/gtest.h>

#include <unistd.h>

#ifdef __linux__
#include <sched.h>
#endif

#include "ferret/pinning.hpp"

using namespace ferret;

TEST(Pinning, PinToExistingCoreSucceeds) {
#ifdef __linux__
  // Query the process affinity mask and pick the lowest available core so
  // this test works on isolated-CPU configurations where core 0 is absent.
  cpu_set_t mask;
  CPU_ZERO(&mask);
  if (sched_getaffinity(0, sizeof(mask), &mask) == 0) {
    for (int core = 0; core < CPU_SETSIZE; ++core) {
      if (CPU_ISSET(core, &mask)) {
        EXPECT_TRUE(pinning::pin_to_core(static_cast<size_t>(core)));
        return;
      }
    }
    GTEST_SKIP() << "no CPUs found in affinity mask";
  }
  // Affinity query failed: fall back to core 0 on a best-effort basis.
  EXPECT_TRUE(pinning::pin_to_core(0));
#else
  EXPECT_TRUE(pinning::pin_to_core(0));
#endif
}

TEST(Pinning, BoostPriorityRequiresPrivilege) {
  if (geteuid() != 0) {
    GTEST_SKIP() << "setpriority(-10) requires root; skipping in unprivileged CI";
  }
  EXPECT_TRUE(pinning::boost_priority());
}

TEST(Pinning, LockMemoryRequiresPrivilege) {
  if (geteuid() != 0) {
    GTEST_SKIP() << "mlockall requires CAP_IPC_LOCK; skipping in unprivileged CI";
  }
  EXPECT_TRUE(pinning::lock_memory());
}

TEST(Pinning, PinToImpossiblyHighCoreReturnsFalse) {
  bool ok = pinning::pin_to_core(99999);
  EXPECT_FALSE(ok);
}

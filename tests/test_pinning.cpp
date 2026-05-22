#include <gtest/gtest.h>

#include <unistd.h>

#include "ferret/pinning.hpp"

using namespace ferret;

TEST(Pinning, PinToExistingCoreSucceeds) { EXPECT_TRUE(pinning::pin_to_core(0)); }

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

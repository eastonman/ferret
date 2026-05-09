#include <gtest/gtest.h>

#include "ferret/pinning.hpp"

using namespace ferret;

// All operations are best-effort. The contract is: returns bool, never
// throws or aborts. We don't assert success because CI runners may
// forbid setpriority/mlockall/affinity.

TEST(Pinning, PinToCoreReturnsBool) {
  bool ok = pinning::pin_to_core(0);
  (void)ok;
  SUCCEED();
}

TEST(Pinning, BoostPriorityReturnsBool) {
  bool ok = pinning::boost_priority();
  (void)ok;
  SUCCEED();
}

TEST(Pinning, LockMemoryReturnsBool) {
  bool ok = pinning::lock_memory();
  (void)ok;
  SUCCEED();
}

TEST(Pinning, PinToImpossiblyHighCoreReturnsFalse) {
  bool ok = pinning::pin_to_core(99999);
  EXPECT_FALSE(ok);
}

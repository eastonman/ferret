#include "ferret/pinning.hpp"

#include <sys/mman.h>
#include <sys/resource.h>

namespace ferret::pinning {

bool boost_priority() { return setpriority(PRIO_PROCESS, 0, -10) == 0; }

bool lock_memory() { return mlockall(MCL_CURRENT | MCL_FUTURE) == 0; }

}  // namespace ferret::pinning

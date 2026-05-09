#pragma once

#include <map>
#include <string>
#include <vector>

#include "ferret/axis.hpp"
#include "ferret/params.hpp"

namespace ferret::sweep {

// Cross-product over each axis's expanded values. If `overrides` contains
// an entry whose key matches an axis name, that override replaces the
// axis's default expansion for this call. Overrides for unknown axes are
// silently ignored. The first axis varies slowest in the output order.
std::vector<Params> expand(
    const SweepAxes& axes,
    const std::map<std::string, std::vector<int64_t>>& overrides);

}  // namespace ferret::sweep

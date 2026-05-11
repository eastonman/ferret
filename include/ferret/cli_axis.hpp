#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ferret/axis.hpp"

namespace ferret {

// Parses a CLI value like "64", "16,32,64", or "1..32768" against a
// declared Axis. The Axis's Kind decides how a "lo..hi" range expands
// (linear for Range, log2 for Log2Range). Throws std::invalid_argument
// on any malformed input.
std::vector<int64_t> parse_cli_axis_value(const std::string& cli_value,
                                          const Axis& axis);

}  // namespace ferret

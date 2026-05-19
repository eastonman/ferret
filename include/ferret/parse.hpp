#pragma once

#include <cstdint>
#include <string>

namespace ferret {

// Parses an int64 from a string. Throws std::invalid_argument on
// empty input, non-numeric input, or trailing junk after the number.
// The error message echoes the offending input so callers can chain it
// into a higher-level context.
int64_t parse_int(const std::string& s);

}  // namespace ferret

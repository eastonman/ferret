#pragma once

#include <string>

namespace ferret {

// Parses a CLI frequency value: "4.521GHz", "100MHz", "250kHz", "42Hz",
// "1.2e9Hz", or a bare number (treated as Hz). Returned value is hertz.
// Throws std::invalid_argument with message prefix "--freq: <reason>: <input>"
// on empty numeric component, trailing junk after the number, non-finite
// (NaN, +/-inf), or non-positive results.
double parse_freq(const std::string& s);

}  // namespace ferret

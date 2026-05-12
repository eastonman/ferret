#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ferret/axis.hpp"

namespace ferret {

// Parses a CLI value like "64", "16,32,64", or "1..32768" against a
// declared Axis. The Axis's Kind decides how a "lo..hi" range expands
// (linear for Range, log2 for Log2Range). Throws std::invalid_argument
// on any malformed input.
std::vector<int64_t> parse_cli_axis_value(const std::string& cli_value, const Axis& axis);

// Parses a single --<option>=<value> scalar (integer).
// Throws std::invalid_argument on non-integer or trailing junk.
int64_t parse_option_value(const std::string& v);

// Turns CLI11's allow_extras() remainder into a name -> value map. Each
// token must be `--name=value`. Throws std::invalid_argument with
// "unexpected argument: <tok>" for tokens that don't start with `--`
// (or are shorter than 3 characters), and "--axis flags must be
// --name=value: <tok>" for tokens without `=`.
std::map<std::string, std::string> parse_extras(const std::vector<std::string>& tokens);

}  // namespace ferret

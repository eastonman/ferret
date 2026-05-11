#include "ferret/cli_axis.hpp"

#include <charconv>
#include <sstream>
#include <stdexcept>

namespace ferret {

namespace {

int64_t parse_int(const std::string& s) {
  if (s.empty()) throw std::invalid_argument("empty number");
  int64_t v = 0;
  auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || p != s.data() + s.size()) {
    throw std::invalid_argument("not an integer: " + s);
  }
  return v;
}

void validate_value_against_kind(int64_t v, const Axis& axis, const std::string& cli_value) {
  if (axis.kind() == Axis::Kind::Log2Range && v <= 0) {
    throw std::invalid_argument("log2 axis '" + axis.name() + "' requires positive values: " + cli_value);
  }
}

}  // namespace

std::vector<int64_t> parse_cli_axis_value(const std::string& cli_value, const Axis& axis) {
  auto dotdot = cli_value.find("..");
  if (dotdot != std::string::npos) {
    std::string lo_s = cli_value.substr(0, dotdot);
    std::string hi_s = cli_value.substr(dotdot + 2);
    if (lo_s.empty() || hi_s.empty()) {
      throw std::invalid_argument("malformed range: " + cli_value);
    }
    int64_t lo = parse_int(lo_s);
    int64_t hi = parse_int(hi_s);
    if (hi < lo) {
      throw std::invalid_argument("range hi < lo: " + cli_value);
    }
    if (axis.kind() == Axis::Kind::Log2Range) {
      return expand_log2_range(lo, hi, cli_value);
    }
    std::vector<int64_t> out;
    for (int64_t v = lo; v <= hi; ++v) out.push_back(v);
    return out;
  }

  std::vector<int64_t> out;
  std::stringstream ss(cli_value);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (item.empty()) {
      throw std::invalid_argument("empty value in list: " + cli_value);
    }
    int64_t v = parse_int(item);
    validate_value_against_kind(v, axis, cli_value);
    out.push_back(v);
  }
  if (out.empty()) {
    throw std::invalid_argument("no values: " + cli_value);
  }
  return out;
}

}  // namespace ferret

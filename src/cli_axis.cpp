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
}  // namespace

std::vector<int64_t> parse_cli_axis_value(const std::string& cli_value,
                                          const Axis& axis) {
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
    std::vector<int64_t> out;
    if (axis.kind() == Axis::Kind::Log2Range) {
      for (int64_t v = lo; v <= hi; v *= 2) out.push_back(v);
    } else {
      for (int64_t v = lo; v <= hi; ++v) out.push_back(v);
    }
    return out;
  }

  std::vector<int64_t> out;
  std::stringstream ss(cli_value);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (item.empty()) {
      throw std::invalid_argument("empty value in list: " + cli_value);
    }
    out.push_back(parse_int(item));
  }
  if (out.empty()) {
    throw std::invalid_argument("no values: " + cli_value);
  }
  return out;
}

}  // namespace ferret

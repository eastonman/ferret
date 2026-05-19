#include "ferret/parse.hpp"

#include <charconv>
#include <stdexcept>

namespace ferret {

int64_t parse_int(const std::string& s) {
  if (s.empty()) {
    throw std::invalid_argument("empty number");
  }
  int64_t v = 0;
  auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{}) {
    throw std::invalid_argument("not an integer: " + s);
  }
  if (p != s.data() + s.size()) {
    throw std::invalid_argument("trailing junk after integer: " + s);
  }
  return v;
}

}  // namespace ferret

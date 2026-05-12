#include "ferret/freq.hpp"

#include <cmath>
#include <stdexcept>

namespace ferret {

double parse_freq(const std::string& s) {
  auto fail = [&](const char* why) { throw std::invalid_argument(std::string("--freq: ") + why + ": " + s); };

  std::string num = s;
  double mult = 1.0;
  auto strip_suffix = [&](const std::string& suf, double m) {
    if (num.size() >= suf.size() && num.ends_with(suf)) {
      num.resize(num.size() - suf.size());
      mult = m;
      return true;
    }
    return false;
  };
  strip_suffix("GHz", 1e9) || strip_suffix("MHz", 1e6) || strip_suffix("kHz", 1e3) || strip_suffix("Hz", 1.0);

  if (num.empty()) {
    fail("empty numeric component");
  }
  size_t consumed = 0;
  double val = 0.0;
  try {
    val = std::stod(num, &consumed);
  } catch (const std::exception&) {
    fail("not a number");
  }
  if (consumed != num.size()) {
    fail("trailing junk after number");
  }
  double hz = val * mult;
  if (!std::isfinite(hz)) {
    fail("must be finite");
  }
  if (!(hz > 0.0)) {
    fail("must be positive");
  }
  return hz;
}

}  // namespace ferret

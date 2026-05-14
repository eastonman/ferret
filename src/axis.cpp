#include "ferret/axis.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace ferret {

namespace {

// Prepends `context: ` to `what` when context is non-empty and throws
// std::invalid_argument. Shared by expand_log2_range/expand_geom_range
// so error messages stay formatted identically.
[[noreturn]] void throw_with_context(std::string_view context, const char* what) {
  std::string msg;
  if (!context.empty()) {
    msg.append(context).append(": ");
  }
  msg.append(what);
  throw std::invalid_argument(msg);
}

}  // namespace

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Axis Axis::range(std::string name, int64_t lo, int64_t hi) {
  Axis a(std::move(name), Kind::Range);
  a.lo_ = lo;
  a.hi_ = hi;
  return a;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Axis Axis::log2_range(std::string name, int64_t lo, int64_t hi) {
  Axis a(std::move(name), Kind::Log2Range);
  a.lo_ = lo;
  a.hi_ = hi;
  return a;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
Axis Axis::geom_range(std::string name, int64_t lo, int64_t hi, int64_t samples_per_octave) {
  Axis a(std::move(name), Kind::GeomRange);
  a.lo_ = lo;
  a.hi_ = hi;
  a.k_ = samples_per_octave;
  return a;
}

Axis Axis::values(std::string name, std::vector<int64_t> vs) {
  Axis a(std::move(name), Kind::Values);
  a.values_ = std::move(vs);
  return a;
}

std::vector<int64_t> Axis::expand() const {
  std::vector<int64_t> out;
  switch (kind_) {
    case Kind::Range:
      for (int64_t v = lo_; v <= hi_; ++v) {
        out.push_back(v);
      }
      return out;
    case Kind::Log2Range:
      return expand_log2_range(lo_, hi_, "Axis '" + name_ + "'");
    case Kind::GeomRange:
      return expand_geom_range(lo_, hi_, k_, "Axis '" + name_ + "'");
    case Kind::Values:
      return values_;
  }
  return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<int64_t> expand_log2_range(int64_t lo, int64_t hi, std::string_view context) {
  if (lo <= 0) {
    throw_with_context(context, "log2 range requires lo > 0");
  }
  std::vector<int64_t> out;
  // Pre-multiply overflow guard (signed overflow is UB).
  constexpr int64_t kHalfMax = std::numeric_limits<int64_t>::max() / 2;
  for (int64_t v = lo; v <= hi;) {
    out.push_back(v);
    if (v > kHalfMax) {
      break;
    }
    v *= 2;
  }
  return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<int64_t> expand_geom_range(int64_t lo, int64_t hi, int64_t k, std::string_view context) {
  if (lo <= 0) {
    throw_with_context(context, "geom range requires lo > 0");
  }
  if (hi < lo) {
    throw_with_context(context, "geom range requires hi >= lo");
  }
  if (k <= 0) {
    throw_with_context(context, "geom range requires samples_per_octave >= 1");
  }

  std::vector<int64_t> out;
  const auto lo_d = static_cast<double>(lo);
  const auto k_d = static_cast<double>(k);
  // Loop bound: floor(k * log2(hi/lo)) + 2. The exact count of points
  // is floor(k * log2(hi/lo)) + 1; the extra +1 leaves slack for
  // floating rounding so the loop exits via the v > hi check below,
  // not the upper bound.
  const auto max_i = static_cast<int64_t>(k_d * std::log2(static_cast<double>(hi) / lo_d)) + 2;
  for (int64_t i = 0; i <= max_i; ++i) {
    double exact = lo_d * std::exp2(static_cast<double>(i) / k_d);
    // Defensive overflow check; reachable only if max_i was too generous.
    if (exact >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
      break;
    }
    auto v = static_cast<int64_t>(std::llround(exact));
    if (v > hi) {
      break;
    }
    if (!out.empty() && v == out.back()) {
      continue;
    }
    out.push_back(v);
  }
  if (out.empty() || out.back() < hi) {
    out.push_back(hi);
  }
  return out;
}

}  // namespace ferret

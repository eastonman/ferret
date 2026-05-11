#include "ferret/axis.hpp"
#include "ferret/params.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace ferret {

void Params::set(const std::string& key, int64_t value) {
  auto [it, inserted] = values_.emplace(key, value);
  if (inserted) {
    order_.push_back(it->first);
  } else {
    it->second = value;
  }
}

bool Params::has(const std::string& key) const { return values_.contains(key); }

int64_t Params::get_raw(const std::string& key) const {
  auto it = values_.find(key);
  if (it == values_.end()) {
    throw std::out_of_range("Params: missing key " + key);
  }
  return it->second;
}

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
    case Kind::Values:
      return values_;
  }
  return out;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<int64_t> expand_log2_range(int64_t lo, int64_t hi, std::string_view context) {
  if (lo <= 0) {
    std::string msg;
    if (!context.empty()) {
      msg.append(context).append(": ");
    }
    msg.append("log2 range requires lo > 0");
    throw std::invalid_argument(msg);
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

}  // namespace ferret

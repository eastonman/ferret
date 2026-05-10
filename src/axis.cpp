#include "ferret/axis.hpp"
#include "ferret/params.hpp"

#include <limits>
#include <stdexcept>
#include <utility>

namespace ferret {

void Params::set(std::string key, int64_t value) {
  auto [it, inserted] = values_.emplace(key, value);
  if (inserted) {
    order_.push_back(it->first);
  } else {
    it->second = value;
  }
}

bool Params::has(const std::string& key) const {
  return values_.find(key) != values_.end();
}

int64_t Params::get_raw(const std::string& key) const {
  auto it = values_.find(key);
  if (it == values_.end()) {
    throw std::out_of_range("Params: missing key " + key);
  }
  return it->second;
}

Axis Axis::range(std::string name, int64_t lo, int64_t hi) {
  Axis a(std::move(name), Kind::Range);
  a.lo_ = lo;
  a.hi_ = hi;
  return a;
}

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
      for (int64_t v = lo_; v <= hi_; ++v) out.push_back(v);
      return out;
    case Kind::Log2Range: {
      if (lo_ <= 0) {
        throw std::invalid_argument(
            "Axis '" + name_ + "': log2 range requires lo > 0");
      }
      // Pre-multiply overflow check (avoids signed overflow UB).
      constexpr int64_t kHalfMax = std::numeric_limits<int64_t>::max() / 2;
      for (int64_t v = lo_; v <= hi_; ) {
        out.push_back(v);
        if (v > kHalfMax) break;
        v *= 2;
      }
      return out;
    }
    case Kind::Values:
      return values_;
  }
  return out;
}

}  // namespace ferret

#pragma once

#include <cstdint>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace ferret {

// Insertion-ordered key/int64 map. All axis values are int64_t because
// every benchmark axis is integral.
class Params {
 public:
  // First insertion appends to the iteration order returned by keys();
  // subsequent overwrites update the value without reordering.
  void set(const std::string& key, int64_t value);

  // Negative int64 would silently wrap when T is unsigned; reject explicitly.
  template <typename T>
  T get(const std::string& key) const {
    int64_t raw = get_raw(key);
    if constexpr (std::is_unsigned_v<T>) {
      if (raw < 0) {
        throw std::invalid_argument("Params::get<unsigned>: negative value for '" + key + "': " + std::to_string(raw));
      }
    }
    return static_cast<T>(raw);
  }

  bool has(const std::string& key) const;
  std::vector<std::string> keys() const { return order_; }
  size_t size() const { return order_.size(); }

 private:
  int64_t get_raw(const std::string& key) const;

  std::unordered_map<std::string, int64_t> values_;
  std::vector<std::string> order_;
};

}  // namespace ferret

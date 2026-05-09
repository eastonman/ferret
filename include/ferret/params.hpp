#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace ferret {

// Insertion-ordered key/int64 map. v1 keeps all axis values as int64_t
// because every benchmark axis is integral.
class Params {
public:
  void set(std::string key, int64_t value);

  template <typename T>
  T get(const std::string& key) const {
    return static_cast<T>(get_raw(key));
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

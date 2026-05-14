#include "ferret/params.hpp"

#include <stdexcept>

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

}  // namespace ferret

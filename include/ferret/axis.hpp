#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ferret {

class Axis {
public:
  enum class Kind { Range, Log2Range, Values };

  static Axis range(std::string name, int64_t lo, int64_t hi);
  static Axis log2_range(std::string name, int64_t lo, int64_t hi);
  static Axis values(std::string name, std::vector<int64_t> vs);

  const std::string& name() const { return name_; }
  Kind kind() const { return kind_; }
  std::vector<int64_t> expand() const;

private:
  Axis(std::string name, Kind kind) : name_(std::move(name)), kind_(kind) {}

  std::string name_;
  Kind kind_;
  int64_t lo_ = 0;
  int64_t hi_ = 0;
  std::vector<int64_t> values_;
};

using SweepAxes = std::vector<Axis>;

}  // namespace ferret

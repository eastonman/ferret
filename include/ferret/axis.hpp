#pragma once

#include <cstdint>
#include <string>
#include <string_view>
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

// Expands a log2 range [lo, hi] into {lo, lo*2, lo*4, ...} up to the
// largest power-of-two not exceeding hi. Throws std::invalid_argument
// when lo <= 0. Stops when the next doubling would overflow int64_t.
// `context` is prepended to the error message (e.g., axis name or CLI
// fragment) so the user sees what value triggered the failure.
std::vector<int64_t> expand_log2_range(int64_t lo, int64_t hi,
                                       std::string_view context = {});

}  // namespace ferret

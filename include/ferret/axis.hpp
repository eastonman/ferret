#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ferret {

// Declarative parameter axis used by a Benchmark's axes() method. Four
// kinds: Range (inclusive linear), Log2Range (powers of two up to hi),
// GeomRange (k samples per octave, generalization of Log2Range),
// Values (literal list). expand() materializes the chosen kind into a
// concrete int64_t value list.
class Axis {
 public:
  enum class Kind { Range, Log2Range, GeomRange, Values };

  // Closed interval [lo, hi]. Step is 1.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  static Axis range(std::string name, int64_t lo, int64_t hi);

  // {lo, lo*2, lo*4, ...} up to the largest power of two <= hi.
  // Delegates to expand_log2_range; throws std::invalid_argument when
  // lo <= 0.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  static Axis log2_range(std::string name, int64_t lo, int64_t hi);

  // Geometric sweep with `samples_per_octave` points per doubling
  // between `lo` and `hi` inclusive. Equivalent to log2_range when
  // samples_per_octave == 1. Delegates to expand_geom_range; throws
  // std::invalid_argument when lo <= 0, hi < lo, or
  // samples_per_octave <= 0.
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  static Axis geom_range(std::string name, int64_t lo, int64_t hi, int64_t samples_per_octave);

  // Uses the supplied list verbatim; no validation.
  static Axis values(std::string name, std::vector<int64_t> vs);

  const std::string& name() const { return name_; }
  Kind kind() const { return kind_; }

  // GeomRange axes return their declared k; all other kinds return 1.
  // Used by the CLI parser to default the `@k` suffix when omitted.
  int64_t samples_per_octave() const { return k_; }

  // May throw on Log2Range/GeomRange via expand_log2_range /
  // expand_geom_range.
  std::vector<int64_t> expand() const;

  // Throws std::invalid_argument when `v` violates the axis kind's
  // invariants. Log2Range and GeomRange require v > 0; Range and Values
  // accept any integer. The error message embeds the axis name.
  void validate(int64_t v) const;

  // Expands a `lo..hi` range token according to this axis's kind. For
  // GeomRange, `at_k` overrides the axis's declared samples_per_octave
  // when non-null. For non-GeomRange axes, a non-null `at_k` is an
  // error (the CLI `@k` suffix is only meaningful for geom axes).
  // Throws std::invalid_argument on violation; the message embeds the
  // axis name.
  std::vector<int64_t> expand_range(int64_t lo, int64_t hi, std::optional<int64_t> at_k) const;

 private:
  Axis(std::string name, Kind kind) : name_(std::move(name)), kind_(kind) {}

  // Materializes the inclusive integer interval [lo, hi] with step 1.
  // Shared by expand() (Kind::Range) and expand_range() (Kind::Range and
  // Kind::Values, where the CLI `lo..hi` token has no kind-specific
  // shape).
  // NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
  static std::vector<int64_t> linear_range(int64_t lo, int64_t hi);

  std::string name_;
  Kind kind_;
  int64_t lo_ = 0;
  int64_t hi_ = 0;
  int64_t k_ = 1;
  std::vector<int64_t> values_;
};

using SweepAxes = std::vector<Axis>;

// Expands a log2 range [lo, hi] into {lo, lo*2, lo*4, ...} up to the
// largest power-of-two not exceeding hi. Throws std::invalid_argument
// when lo <= 0. Stops when the next doubling would overflow int64_t.
// `context` is prepended to the error message (e.g., axis name or CLI
// fragment) so the user sees what value triggered the failure.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<int64_t> expand_log2_range(int64_t lo, int64_t hi, std::string_view context = {});

// Expands a geometric range [lo, hi] sampling `k` points per octave:
// {round(lo * 2^(i/k))} for i = 0, 1, ... while the value <= hi.
// Adjacent duplicate rounded values are deduped. `hi` is appended as
// the final point when the natural sequence does not land on it. k=1
// reproduces expand_log2_range exactly. Throws std::invalid_argument
// when lo <= 0, hi < lo, or k <= 0. `context` is prepended to the
// error message.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<int64_t> expand_geom_range(int64_t lo, int64_t hi, int64_t k, std::string_view context = {});

}  // namespace ferret

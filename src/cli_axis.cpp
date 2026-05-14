#include "ferret/cli_axis.hpp"

#include <charconv>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace ferret {

namespace {

int64_t parse_int(const std::string& s) {
  if (s.empty()) {
    throw std::invalid_argument("empty number");
  }
  int64_t v = 0;
  auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
  if (ec != std::errc{} || p != s.data() + s.size()) {
    throw std::invalid_argument("not an integer: " + s);
  }
  return v;
}

void validate_value_against_kind(int64_t v, const Axis& axis, const std::string& cli_value) {
  if ((axis.kind() == Axis::Kind::Log2Range || axis.kind() == Axis::Kind::GeomRange) && v <= 0) {
    throw std::invalid_argument("axis '" + axis.name() + "' requires positive values: " + cli_value);
  }
}

// Splits `hi_and_suffix` at the `@` separator into the `hi` token and the
// optional k value. Validates that `@k` is only used with GeomRange axes
// and that k is a positive integer. Returns std::nullopt for the k field
// when the input has no `@` suffix.
struct HiAndK {
  std::string hi_s;
  std::optional<int64_t> k;
};
HiAndK parse_at_suffix(const std::string& hi_and_suffix, const Axis& axis, const std::string& cli_value) {
  auto at = hi_and_suffix.find('@');
  if (at == std::string::npos) {
    return {.hi_s = hi_and_suffix, .k = std::nullopt};
  }
  std::string hi_s = hi_and_suffix.substr(0, at);
  if (hi_s.empty()) {
    throw std::invalid_argument("malformed range: " + cli_value);
  }
  std::string k_s = hi_and_suffix.substr(at + 1);
  if (k_s.empty()) {
    throw std::invalid_argument("malformed @k suffix: " + cli_value);
  }
  if (axis.kind() != Axis::Kind::GeomRange) {
    throw std::invalid_argument("axis '" + axis.name() + "': @k is only valid for geom_range axes: " + cli_value);
  }
  int64_t k = parse_int(k_s);
  if (k <= 0) {
    throw std::invalid_argument("@k must be >= 1: " + cli_value);
  }
  return {.hi_s = std::move(hi_s), .k = k};
}

// Expands a `lo..hi[@k]` range token according to `axis` kind. `at_k`
// is the k explicitly supplied via `@k` (or nullopt when omitted); for
// GeomRange axes it falls back to the axis's declared samples_per_octave.
std::vector<int64_t> expand_range_token(int64_t lo, int64_t hi, std::optional<int64_t> at_k, const Axis& axis,
                                        const std::string& cli_value) {
  std::vector<int64_t> out;
  switch (axis.kind()) {
    case Axis::Kind::GeomRange:
      return expand_geom_range(lo, hi, at_k.value_or(axis.samples_per_octave()), cli_value);
    case Axis::Kind::Log2Range:
      return expand_log2_range(lo, hi, cli_value);
    case Axis::Kind::Range:
    case Axis::Kind::Values:
      for (int64_t v = lo; v <= hi; ++v) {
        out.push_back(v);
      }
      return out;
  }
  return out;
}

}  // namespace

std::vector<int64_t> parse_cli_axis_value(const std::string& cli_value, const Axis& axis) {
  auto dotdot = cli_value.find("..");
  if (dotdot != std::string::npos) {
    std::string lo_s = cli_value.substr(0, dotdot);
    auto [hi_s, at_k] = parse_at_suffix(cli_value.substr(dotdot + 2), axis, cli_value);

    if (lo_s.empty() || hi_s.empty()) {
      throw std::invalid_argument("malformed range: " + cli_value);
    }
    int64_t lo = parse_int(lo_s);
    int64_t hi = parse_int(hi_s);
    if (hi < lo) {
      throw std::invalid_argument("range hi < lo: " + cli_value);
    }
    return expand_range_token(lo, hi, at_k, axis, cli_value);
  }

  std::vector<int64_t> out;
  std::stringstream ss(cli_value);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (item.empty()) {
      throw std::invalid_argument("empty value in list: " + cli_value);
    }
    int64_t v = parse_int(item);
    validate_value_against_kind(v, axis, cli_value);
    out.push_back(v);
  }
  if (out.empty()) {
    throw std::invalid_argument("no values: " + cli_value);
  }
  return out;
}

int64_t parse_option_value(const std::string& v) {
  if (v.empty()) {
    throw std::invalid_argument("not an integer: " + v);
  }
  int64_t parsed = 0;
  auto [p, ec] = std::from_chars(v.data(), v.data() + v.size(), parsed);
  if (ec != std::errc{}) {
    throw std::invalid_argument("not an integer: " + v);
  }
  if (p != v.data() + v.size()) {
    throw std::invalid_argument("trailing junk after integer: " + v);
  }
  return parsed;
}

std::map<std::string, std::string> parse_extras(const std::vector<std::string>& tokens) {
  std::map<std::string, std::string> out;
  for (const auto& tok : tokens) {
    if (tok.size() < 3 || tok[0] != '-' || tok[1] != '-') {
      throw std::invalid_argument("unexpected argument: " + tok);
    }
    auto eq = tok.find('=');
    if (eq == std::string::npos) {
      throw std::invalid_argument("--axis flags must be --name=value: " + tok);
    }
    out[tok.substr(2, eq - 2)] = tok.substr(eq + 1);
  }
  return out;
}

}  // namespace ferret

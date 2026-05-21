#include "ferret/cli_axis.hpp"

#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>

#include "ferret/parse.hpp"

namespace ferret {

namespace {

// Splits `hi_and_suffix` at the `@` separator into the `hi` token and the
// optional k value. Validates that k is a positive integer. Returns
// std::nullopt for the k field when the input has no `@` suffix.
struct HiAndK {
  std::string hi_s;
  std::optional<int64_t> k;
};
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
HiAndK parse_at_suffix(const std::string& hi_and_suffix, const std::string& cli_value) {
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
  int64_t k = parse_int(k_s);
  if (k <= 0) {
    throw std::invalid_argument("@k must be >= 1: " + cli_value);
  }
  return {.hi_s = std::move(hi_s), .k = k};
}

}  // namespace

std::vector<int64_t> parse_cli_axis_value(const std::string& cli_value, const Axis& axis) {
  auto dotdot = cli_value.find("..");
  if (dotdot != std::string::npos) {
    std::string lo_s = cli_value.substr(0, dotdot);
    auto [hi_s, at_k] = parse_at_suffix(cli_value.substr(dotdot + 2), cli_value);

    if (lo_s.empty() || hi_s.empty()) {
      throw std::invalid_argument("malformed range: " + cli_value);
    }
    int64_t lo = parse_int(lo_s);
    int64_t hi = parse_int(hi_s);
    if (hi < lo) {
      throw std::invalid_argument("range hi < lo: " + cli_value);
    }
    try {
      return axis.expand_range(lo, hi, at_k);
    } catch (const std::invalid_argument& e) {
      // Append the cli_value context so the diagnostic names the
      // offending input token.
      throw std::invalid_argument(std::string(e.what()) + " (in: " + cli_value + ")");
    }
  }

  std::vector<int64_t> out;
  std::stringstream ss(cli_value);
  std::string item;
  while (std::getline(ss, item, ',')) {
    if (item.empty()) {
      throw std::invalid_argument("empty value in list: " + cli_value);
    }
    int64_t v = parse_int(item);
    try {
      axis.validate(v);
    } catch (const std::invalid_argument&) {
      throw std::invalid_argument("axis '" + axis.name() + "' requires positive values: " + cli_value);
    }
    out.push_back(v);
  }
  if (out.empty()) {
    throw std::invalid_argument("no values: " + cli_value);
  }
  return out;
}

int64_t parse_option_value(const std::string& v) { return parse_int(v); }

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

#include "ferret/sweep.hpp"

namespace ferret::sweep {

std::vector<Params> expand(
    const SweepAxes& axes,
    const std::map<std::string, std::vector<int64_t>>& overrides) {
  std::vector<std::pair<std::string, std::vector<int64_t>>> resolved;
  resolved.reserve(axes.size());
  for (const Axis& a : axes) {
    auto it = overrides.find(a.name());
    resolved.emplace_back(a.name(),
                          it != overrides.end() ? it->second : a.expand());
  }

  std::vector<Params> rows;
  if (resolved.empty()) {
    rows.emplace_back();
    return rows;
  }

  std::vector<size_t> indices(resolved.size(), 0);
  while (true) {
    Params p;
    for (size_t i = 0; i < resolved.size(); ++i) {
      p.set(resolved[i].first, resolved[i].second[indices[i]]);
    }
    rows.push_back(std::move(p));

    size_t i = resolved.size();
    while (i > 0) {
      --i;
      ++indices[i];
      if (indices[i] < resolved[i].second.size()) break;
      indices[i] = 0;
      if (i == 0) return rows;
    }
  }
}

}  // namespace ferret::sweep

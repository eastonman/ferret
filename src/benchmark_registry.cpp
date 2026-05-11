#include "ferret/benchmark.hpp"

#include <algorithm>
#include <map>

namespace ferret {

namespace {
std::map<std::string, BenchmarkRegistry::Factory>& store() {
  static std::map<std::string, BenchmarkRegistry::Factory> s;
  return s;
}
}  // namespace

void BenchmarkRegistry::register_benchmark(std::string name, Factory f) {
  store().emplace(std::move(name), std::move(f));
}

std::unique_ptr<Benchmark> BenchmarkRegistry::create(const std::string& name) {
  auto it = store().find(name);
  if (it == store().end()) {
    return nullptr;
  }
  return it->second();
}

std::vector<std::string> BenchmarkRegistry::names() {
  std::vector<std::string> out;
  for (const auto& [k, _] : store()) {
    out.push_back(k);
  }
  std::ranges::sort(out);
  return out;
}

}  // namespace ferret

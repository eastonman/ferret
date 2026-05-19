#include "ferret/permute.hpp"

#include <numeric>
#include <random>
#include <utility>

namespace ferret {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)  // convertible types on this platform
std::vector<size_t> sattolo_cycle(size_t n, uint64_t seed) {
  std::vector<size_t> next(n);
  std::iota(next.begin(), next.end(), size_t{0});
  if (n < 2) {
    return next;
  }
  std::mt19937_64 rng(seed);
  for (size_t i = n - 1; i > 0; --i) {
    std::uniform_int_distribution<size_t> dist(0, i - 1);
    std::swap(next[i], next[dist(rng)]);
  }
  return next;
}

uint64_t mix_seed(uint64_t seed, uint64_t x, uint64_t y) {
  return seed ^ (x * 0x9E3779B97F4A7C15ULL) ^ (y * 0xBF58476D1CE4E5B9ULL);
}

}  // namespace ferret

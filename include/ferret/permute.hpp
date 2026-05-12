#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ferret {

// Sattolo's algorithm: a single Hamiltonian cycle over {0,…,n-1} seeded
// by `seed`. n==0 returns {}; n==1 returns {0} (no cycle possible).
std::vector<size_t> sattolo_cycle(size_t n, uint64_t seed);

}  // namespace ferret

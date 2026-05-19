#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ferret {

// Sattolo's algorithm: a single Hamiltonian cycle over {0,…,n-1} seeded
// by `seed`. n==0 returns {}; n==1 returns {0} (no cycle possible).
std::vector<size_t> sattolo_cycle(size_t n, uint64_t seed);

// Mixes a seed with two auxiliary integers to produce a well-distributed
// uint64 with good avalanche behavior. The mixer is an XOR-based fold,
// so it is not injective — distinct (seed, x, y) tuples can collide,
// but the probability is negligible for the call patterns benchmarks
// use. Callers needing per-parameter-point random variation (Sattolo
// cycles, pattern fills) hit this rather than recomputing the magic
// constants at each site.
//
// Constants are golden ratio (0x9E37…7C15) and the Murmur3 fmix64
// xorshift multiplier (0xBF58…E5B9) — both have good avalanche behavior
// and the literature treats them as standard mixing constants.
uint64_t mix_seed(uint64_t seed, uint64_t x, uint64_t y);

}  // namespace ferret

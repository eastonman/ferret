#include "ferret/runner.hpp"

#include "ferret/timing.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace ferret::runner {

MeasurementRow measure(KernelFn fn, size_t iters, size_t sites, int K, int warmup) {
  if (K <= 0) {
    throw std::invalid_argument("runner::measure: K (reps) must be >= 1");
  }
  if (warmup < 0) {
    throw std::invalid_argument("runner::measure: warmup must be >= 0");
  }
  MeasurementRow row;
  row.iters = iters;
  row.sites = sites;
  row.reps = static_cast<size_t>(K);

  for (int i = 0; i < warmup; ++i) fn();

  std::vector<uint64_t> samples;
  samples.reserve(K);
  for (int i = 0; i < K; ++i) {
    uint64_t t0 = timing::arch_now_ticks();
    fn();
    uint64_t t1 = timing::arch_now_ticks();
    samples.push_back(t1 - t0);
  }

  std::sort(samples.begin(), samples.end());
  row.ticks_min = samples.front();
  row.ticks_median = samples[samples.size() / 2];
  return row;
}

}  // namespace ferret::runner

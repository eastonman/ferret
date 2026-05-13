#include "ferret/runner.hpp"

#include "ferret/timing.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace ferret::runner {

MeasurementRow measure(KernelFn fn, const MeasureOptions& opts) {
  if (opts.reps <= 0) {
    throw std::invalid_argument("runner::measure: reps must be >= 1");
  }
  if (opts.warmup < 0) {
    throw std::invalid_argument("runner::measure: warmup must be >= 0");
  }
  MeasurementRow row;
  row.iters = opts.iters;
  row.sites = opts.sites;
  row.reps = static_cast<size_t>(opts.reps);

  for (int i = 0; i < opts.warmup; ++i) {
    fn();
  }

  std::vector<uint64_t> samples;
  samples.reserve(opts.reps);
  for (int i = 0; i < opts.reps; ++i) {
    uint64_t t0 = timing::arch_now_ticks();
    fn();
    uint64_t t1 = timing::arch_now_ticks();
    samples.push_back(t1 - t0);
  }

  std::ranges::sort(samples);
  row.ticks_min = samples.front();
  row.ticks_median = samples[samples.size() / 2];
  return row;
}

}  // namespace ferret::runner

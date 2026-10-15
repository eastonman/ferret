#include "ferret/runner.hpp"

#include "ferret/benchmark.hpp"
#include "ferret/jit.hpp"
#include "ferret/log.hpp"
#include "ferret/params.hpp"
#include "ferret/timing.hpp"

#include <algorithm>
#include <stdexcept>
#include <vector>

namespace flog = ferret::log;

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

MeasurementRow single_kernel_measure(Benchmark& b, const Params& p, int reps, int warmup) {
  size_t pre_iters = b.iterations(p);
  size_t pre_sites = b.sites_per_kernel(p);
  if (pre_iters == 0 || pre_sites == 0) {
    throw std::invalid_argument("single_kernel_measure: iterations and sites_per_kernel must be > 0");
  }
  JittedKernel kern(b, p);
  if (!kern.ok()) {
    MeasurementRow row;
    row.jit_failed = true;
    row.iters = pre_iters;
    row.sites = pre_sites;
    flog::warn("jit failure in {}; emitting empty row", b.name());
    return row;
  }
  flog::info("jit kernel: {} bytes", kern.code_size());
  return measure(kern.fn(), {.iters = pre_iters, .sites = pre_sites, .reps = reps, .warmup = warmup});
}

}  // namespace ferret::runner

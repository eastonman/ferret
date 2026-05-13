#include "ferret/run_command.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <vector>

#include "ferret/benchmark.hpp"
#include "ferret/cli_axis.hpp"
#include "ferret/csv.hpp"
#include "ferret/jit.hpp"
#include "ferret/log.hpp"
#include "ferret/params.hpp"
#include "ferret/pinning.hpp"
#include "ferret/runner.hpp"
#include "ferret/sweep.hpp"
#include "ferret/timing.hpp"

namespace flog = ferret::log;

namespace ferret {
namespace {

struct ClassifiedOverrides {
  std::map<std::string, std::vector<int64_t>> axis_values;
  std::map<std::string, int64_t> option_values;
};

std::optional<ClassifiedOverrides> classify_overrides(const std::string& bench_name, const SweepAxes& axes,
                                                      const BenchOptions& options,
                                                      const std::map<std::string, std::string>& raw) {
  ClassifiedOverrides out;
  for (const auto& o : options) {
    out.option_values[o.name] = o.default_value;
  }
  for (const auto& [k, v] : raw) {
    const Axis* axis_match = nullptr;
    for (const auto& a : axes) {
      if (a.name() == k) {
        axis_match = &a;
        break;
      }
    }
    const BenchOption* opt_match = nullptr;
    for (const auto& o : options) {
      if (o.name == k) {
        opt_match = &o;
        break;
      }
    }
    if (axis_match != nullptr) {
      try {
        out.axis_values[k] = parse_cli_axis_value(v, *axis_match);
      } catch (const std::exception& e) {
        flog::error("invalid value for --{}: {}", k, e.what());
        return std::nullopt;
      }
    } else if (opt_match != nullptr) {
      try {
        out.option_values[k] = parse_option_value(v);
      } catch (const std::exception& e) {
        flog::error("invalid value for --{}: {}", k, e.what());
        return std::nullopt;
      }
    } else {
      flog::error("unknown axis or option --{} for benchmark {}", k, bench_name);
      return std::nullopt;
    }
  }
  return out;
}

void inject_options(std::vector<Params>& rows, const std::map<std::string, int64_t>& options, int64_t seed) {
  for (auto& p : rows) {
    for (const auto& [k, v] : options) {
      p.set(k, v);
    }
    p.set("seed", seed);
  }
}

void apply_pinning(int core) {
  if (core >= 0 && !pinning::pin_to_core(core)) {
    flog::warn("pin_to_core({}) failed", core);
  }
  if (!pinning::boost_priority()) {
    flog::warn("boost_priority failed");
  }
  if (!pinning::lock_memory()) {
    flog::warn("mlockall failed");
  }
}

std::ostream* open_output(const std::string& path, std::ofstream& ofs) {
  if (path.empty()) {
    return &std::cout;
  }
  ofs.open(path);
  if (!ofs) {
    flog::error("cannot open output: {}", path);
    return nullptr;
  }
  return &ofs;
}

std::vector<std::string> column_names(const SweepAxes& axes, const BenchOptions& options) {
  std::vector<std::string> cols;
  cols.reserve(axes.size() + options.size() + 1);
  for (const auto& a : axes) {
    cols.push_back(a.name());
  }
  for (const auto& o : options) {
    cols.push_back(o.name);
  }
  cols.emplace_back("seed");
  return cols;
}

struct MeasuredRow {
  Params params;
  MeasurementRow row;
};

std::optional<std::vector<MeasuredRow>> measure_all(Benchmark& bench, const std::vector<Params>& rows, int reps,
                                                    int warmup) {
  std::vector<MeasuredRow> out;
  out.reserve(rows.size());
  for (const auto& p : rows) {
    MeasurementRow m;
    try {
      size_t pre_iters = bench.iterations(p);
      size_t pre_sites = bench.sites_per_kernel(p);
      if (pre_iters == 0 || pre_sites == 0) {
        flog::error("invalid params: yields zero work (iterations={}, sites_per_kernel={})", pre_iters, pre_sites);
        return std::nullopt;
      }
      JittedKernel kern(bench, p);
      if (!kern.ok()) {
        m.jit_failed = true;
        m.iters = pre_iters;
        m.sites = pre_sites;
        flog::warn("sljit_error on params; emitting empty row");
      } else {
        flog::info("jit kernel: {} bytes", kern.code_size());
        m = runner::measure(kern.fn(), {.iters = pre_iters, .sites = pre_sites, .reps = reps, .warmup = warmup});
      }
    } catch (const std::exception& e) {
      flog::error("benchmark error on params: {}", e.what());
      return std::nullopt;
    }
    out.push_back({p, m});
  }
  return out;
}

void emit_csv(std::ostream& out, const std::string& bench_name, const std::vector<std::string>& cols,
              std::optional<double> freq_hz, double tpns, const std::vector<MeasuredRow>& rows) {
  CsvWriter writer(out, bench_name, cols, freq_hz);
  writer.write_header();
  for (const auto& r : rows) {
    writer.write_row(r.params, r.row, tpns);
  }
  out.flush();
}

}  // namespace

int run(const RunOptions& opts) {
  auto bench = BenchmarkRegistry::create(opts.name);
  if (!bench) {
    flog::error("unknown benchmark '{}'. Try `ferret list`.", opts.name);
    return 2;
  }

  SweepAxes axes = bench->axes();
  BenchOptions options = bench->options();

  auto classified = classify_overrides(opts.name, axes, options, opts.overrides);
  if (!classified) {
    return 2;
  }

  std::vector<Params> rows;
  try {
    rows = sweep::expand(axes, classified->axis_values);
  } catch (const std::exception& e) {
    flog::error("invalid sweep: {}", e.what());
    return 2;
  }

  inject_options(rows, classified->option_values, opts.seed);
  apply_pinning(opts.core);

  std::ofstream ofs;
  std::ostream* out = open_output(opts.out_path, ofs);
  if (out == nullptr) {
    return 2;
  }

  // Non-finite tpns would propagate NaN into CSV.
  double tpns = timing::ticks_per_ns();
  if (!std::isfinite(tpns) || !(tpns > 0.0)) {
    flog::error("ticks_per_ns calibration returned non-finite or non-positive value: {}", tpns);
    return 2;
  }

  // No partial output: buffer all rows, emit only after every row succeeded.
  auto measured = measure_all(*bench, rows, opts.reps, opts.warmup);
  if (!measured) {
    return 2;
  }

  emit_csv(*out, opts.name, column_names(axes, options), opts.freq_hz, tpns, *measured);
  return 0;
}

int list_command() {
  for (const auto& n : BenchmarkRegistry::names()) {
    std::cout << n << "\n";
  }
  return 0;
}

}  // namespace ferret

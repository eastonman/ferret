#include "ferret/run_command.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <ostream>
#include <stdexcept>
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
  std::map<std::string, const Axis*> axis_by_name;
  std::map<std::string, const BenchOption*> option_by_name;
  for (const auto& a : axes) {
    axis_by_name.emplace(a.name(), &a);
  }
  for (const auto& o : options) {
    option_by_name.emplace(o.name, &o);
    out.option_values[o.name] = o.default_value;
  }
  for (const auto& [k, v] : raw) {
    if (auto axis_it = axis_by_name.find(k); axis_it != axis_by_name.end()) {
      try {
        out.axis_values[k] = parse_cli_axis_value(v, *axis_it->second);
      } catch (const std::exception& e) {
        flog::error("invalid value for --{}: {}", k, e.what());
        return std::nullopt;
      }
    } else if (auto opt_it = option_by_name.find(k); opt_it != option_by_name.end()) {
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
    size_t pre_iters = 0;
    size_t pre_sites = 0;
    try {
      pre_iters = bench.iterations(p);
      pre_sites = bench.sites_per_kernel(p);
    } catch (const std::exception& e) {
      flog::error("invalid params: {}", e.what());
      return std::nullopt;
    }
    if (pre_iters == 0 || pre_sites == 0) {
      flog::error("invalid params: yields zero work (iterations={}, sites_per_kernel={})", pre_iters, pre_sites);
      return std::nullopt;
    }
    try {
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
    } catch (const std::invalid_argument& e) {
      flog::error("benchmark error on params: {}", e.what());
      return std::nullopt;
    } catch (const std::exception& e) {
      m.jit_failed = true;
      m.iters = pre_iters;
      m.sites = pre_sites;
      flog::warn("benchmark codegen failed on params: {}; emitting empty row", e.what());
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

struct RunInputs {
  std::unique_ptr<Benchmark> bench;
  std::vector<Params> rows;
  std::vector<std::string> cols;
};

std::optional<RunInputs> build_run_inputs(const RunOptions& opts) {
  auto bench = BenchmarkRegistry::create(opts.name);
  if (!bench) {
    flog::error("unknown benchmark '{}'. Try `ferret list`.", opts.name);
    return std::nullopt;
  }
  SweepAxes axes = bench->axes();
  BenchOptions options = bench->options();

  auto classified = classify_overrides(opts.name, axes, options, opts.overrides);
  if (!classified) {
    return std::nullopt;
  }

  std::vector<Params> rows;
  try {
    rows = sweep::expand(axes, classified->axis_values);
  } catch (const std::exception& e) {
    flog::error("invalid sweep: {}", e.what());
    return std::nullopt;
  }
  inject_options(rows, classified->option_values, opts.seed);

  auto cols = column_names(axes, options);
  return RunInputs{
      .bench = std::move(bench),
      .rows = std::move(rows),
      .cols = std::move(cols),
  };
}

}  // namespace

int run(const RunOptions& opts) {
  auto inputs = build_run_inputs(opts);
  if (!inputs) {
    return 2;
  }

  apply_pinning(opts.core);

  std::ofstream ofs;
  std::ostream* out = open_output(opts.out_path, ofs);
  if (out == nullptr) {
    return 2;
  }

  double tpns = timing::ticks_per_ns();
  if (!std::isfinite(tpns) || !(tpns > 0.0)) {
    flog::error("ticks_per_ns calibration returned non-finite or non-positive value: {}", tpns);
    return 2;
  }

  auto measured = measure_all(*inputs->bench, inputs->rows, opts.reps, opts.warmup);
  if (!measured) {
    return 2;
  }

  emit_csv(*out, opts.name, inputs->cols, opts.freq_hz, tpns, *measured);
  return 0;
}

int list_command() {
  for (const auto& n : BenchmarkRegistry::names()) {
    std::cout << n << "\n";
  }
  return 0;
}

}  // namespace ferret

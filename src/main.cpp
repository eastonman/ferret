#include <CLI/CLI.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "ferret/benchmark.hpp"
#include "ferret/cli_axis.hpp"
#include "ferret/csv.hpp"
#include "ferret/freq.hpp"
#include "ferret/jit.hpp"
#include "ferret/log.hpp"
#include "ferret/pinning.hpp"
#include "ferret/runner.hpp"
#include "ferret/sweep.hpp"
#include "ferret/timing.hpp"

// Aliased as `flog` (not `log`) to avoid colliding with the C library
// `::log(double)` declared in <math.h> on glibc, which leaks into the
// global namespace via transitive includes (e.g., <filesystem>).
namespace flog = ferret::log;

namespace {

int do_list() {
  for (const auto& n : ferret::BenchmarkRegistry::names()) {
    std::cout << n << "\n";
  }
  return 0;
}

// NOLINTBEGIN(readability-function-cognitive-complexity,bugprone-easily-swappable-parameters)
int do_run(const std::string& name, const std::map<std::string, std::string>& cli_axis_overrides,
           const std::string& out_path, int core, std::optional<double> freq_hz, int K, int warmup, int64_t seed) {
  // NOLINTEND(readability-function-cognitive-complexity,bugprone-easily-swappable-parameters)
  auto bench = ferret::BenchmarkRegistry::create(name);
  if (!bench) {
    flog::error("unknown benchmark '{}'. Try `ferret list`.", name);
    return 2;
  }

  ferret::SweepAxes axes = bench->axes();
  ferret::BenchOptions options = bench->options();
  std::map<std::string, int64_t> option_values;
  for (const auto& o : options) {
    option_values[o.name] = o.default_value;
  }

  std::map<std::string, std::vector<int64_t>> overrides;
  for (const auto& [k, v] : cli_axis_overrides) {
    const ferret::Axis* axis_match = nullptr;
    for (const auto& a : axes) {
      if (a.name() == k) {
        axis_match = &a;
        break;
      }
    }
    const ferret::BenchOption* opt_match = nullptr;
    for (const auto& o : options) {
      if (o.name == k) {
        opt_match = &o;
        break;
      }
    }
    if (axis_match != nullptr) {
      try {
        overrides[k] = ferret::parse_cli_axis_value(v, *axis_match);
      } catch (const std::exception& e) {
        flog::error("invalid value for --{}: {}", k, e.what());
        return 2;
      }
    } else if (opt_match != nullptr) {
      try {
        option_values[k] = ferret::parse_option_value(v);
      } catch (const std::exception& e) {
        flog::error("invalid value for --{}: {}", k, e.what());
        return 2;
      }
    } else {
      flog::error("unknown axis or option --{} for benchmark {}", k, name);
      return 2;
    }
  }

  std::vector<ferret::Params> rows;
  try {
    rows = ferret::sweep::expand(axes, overrides);
  } catch (const std::exception& e) {
    flog::error("invalid sweep: {}", e.what());
    return 2;
  }

  // Inject non-swept options + seed into every row so they're recorded in CSV.
  for (auto& p : rows) {
    for (const auto& [k, v] : option_values) {
      p.set(k, v);
    }
    p.set("seed", seed);
  }

  if (core >= 0) {
    if (!ferret::pinning::pin_to_core(core)) {
      flog::warn("pin_to_core({}) failed", core);
    }
  }
  if (!ferret::pinning::boost_priority()) {
    flog::warn("boost_priority failed");
  }
  if (!ferret::pinning::lock_memory()) {
    flog::warn("mlockall failed");
  }

  std::ofstream ofs;
  std::ostream* out_stream = &std::cout;
  if (!out_path.empty()) {
    ofs.open(out_path);
    if (!ofs) {
      flog::error("cannot open output: {}", out_path);
      return 2;
    }
    out_stream = &ofs;
  }

  std::vector<std::string> axis_cols;
  for (const auto& a : axes) {
    axis_cols.push_back(a.name());
  }
  for (const auto& o : options) {
    axis_cols.push_back(o.name);
  }
  axis_cols.emplace_back("seed");

  // tpns is the divisor for every CSV row; non-finite or zero would leak
  // inf/nan into output.
  double tpns = ferret::timing::ticks_per_ns();
  if (!std::isfinite(tpns) || !(tpns > 0.0)) {
    flog::error("ticks_per_ns calibration returned non-finite or non-positive value: {}", tpns);
    return 2;
  }

  // Spec §7 class-1: benchmark-parameter errors must produce no partial
  // output. Collect every row's measurement in memory and emit only on
  // full success.
  std::vector<std::pair<ferret::Params, ferret::MeasurementRow>> buffered;
  buffered.reserve(rows.size());

  for (const auto& p : rows) {
    ferret::MeasurementRow m;
    try {
      size_t pre_iters = bench->iterations(p);
      size_t pre_sites = bench->sites_per_kernel(p);
      if (pre_iters == 0 || pre_sites == 0) {
        flog::error("invalid params: yields zero work (iterations={}, sites_per_kernel={})", pre_iters, pre_sites);
        return 2;
      }

      ferret::JittedKernel kern(*bench, p);
      if (!kern.ok()) {
        m.jit_failed = true;
        m.iters = pre_iters;
        m.sites = pre_sites;
        flog::warn("sljit_error on params; emitting empty row");
      } else {
        flog::info("jit kernel: {} bytes", kern.code_size());
        m = ferret::runner::measure(kern.fn(), pre_iters, pre_sites, K, warmup);
      }
    } catch (const std::exception& e) {
      flog::error("benchmark error on params: {}", e.what());
      return 2;
    }
    buffered.emplace_back(p, m);
  }

  ferret::CsvWriter writer(*out_stream, name, axis_cols, freq_hz);
  writer.write_header();
  for (const auto& [p, m] : buffered) {
    writer.write_row(p, m, tpns);
  }
  out_stream->flush();

  return 0;
}

}  // namespace

// NOLINTNEXTLINE(bugprone-exception-escape)
int main(int argc, char** argv) {
  flog::init();

  CLI::App app{"ferret — frontend reverse-engineering toolkit"};
  app.require_subcommand(1);

  auto* list_cmd = app.add_subcommand("list", "List registered benchmarks");

  auto* run_cmd = app.add_subcommand("run", "Run a benchmark");
  std::string name;
  run_cmd->add_option("name", name, "benchmark name")->required();

  // --log-level is attached to run_cmd, not app, because run_cmd uses
  // allow_extras() and would otherwise swallow it as an --<axis>= override.
  std::string log_level_str = "warn";
  run_cmd
      ->add_option("--log-level", log_level_str, "log level: trace|debug|info|warn|error|critical|off (default warn)")
      ->check(CLI::IsMember({"trace", "debug", "info", "warn", "warning", "error", "critical", "off"}));

  std::string out_path;
  run_cmd->add_option("--out", out_path, "CSV output path (default stdout)");

  int core = -1;
  run_cmd->add_option("--core", core, "core to pin to (default: no pin)");

  std::string freq_str;
  run_cmd->add_option("--freq", freq_str, "core frequency, e.g. 4.521GHz; enables cycles_per_site columns");

  int K = 7;
  run_cmd->add_option("--reps", K, "number of timed repetitions per param point (default 7)");

  int warmup = 1;
  run_cmd->add_option("--warmup", warmup, "warmup calls before each measurement (default 1)");

  int64_t seed = 1;
  run_cmd->add_option("--seed", seed, "RNG seed for benchmarks that randomize (default 1)");

  run_cmd->allow_extras();

  CLI11_PARSE(app, argc, argv);

  flog::set_level(flog::parse_level(log_level_str));

  if (*list_cmd) {
    return do_list();
  }

  if (*run_cmd) {
    if (K < 1) {
      flog::error("--reps must be >= 1 (got {})", K);
      return 2;
    }
    if (warmup < 0) {
      flog::error("--warmup must be >= 0 (got {})", warmup);
      return 2;
    }

    std::map<std::string, std::string> overrides;
    try {
      overrides = ferret::parse_extras(run_cmd->remaining());
    } catch (const std::exception& e) {
      flog::error("{}", e.what());
      return 2;
    }

    std::optional<double> freq_hz;
    if (!freq_str.empty()) {
      try {
        freq_hz = ferret::parse_freq(freq_str);
      } catch (const std::exception& e) {
        flog::error("invalid {}", e.what());
        return 2;
      }
    }
    return do_run(name, overrides, out_path, core, freq_hz, K, warmup, seed);
  }

  return 0;
}

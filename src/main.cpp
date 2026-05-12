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

extern "C" {
#include <sljitLir.h>
}

#include "ferret/benchmark.hpp"
#include "ferret/cli_axis.hpp"
#include "ferret/csv.hpp"
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

double parse_freq(const std::string& s) {
  auto fail = [&](const char* why) { throw std::invalid_argument(std::string("--freq: ") + why + ": " + s); };

  std::string num = s;
  double mult = 1.0;
  auto strip_suffix = [&](const std::string& suf, double m) {
    if (num.size() >= suf.size() && num.ends_with(suf)) {
      num.resize(num.size() - suf.size());
      mult = m;
      return true;
    }
    return false;
  };
  strip_suffix("GHz", 1e9) || strip_suffix("MHz", 1e6) || strip_suffix("kHz", 1e3) || strip_suffix("Hz", 1.0);

  if (num.empty()) {
    fail("empty numeric component");
  }
  size_t consumed = 0;
  double val = 0.0;
  try {
    val = std::stod(num, &consumed);
  } catch (const std::exception&) {
    fail("not a number");
  }
  if (consumed != num.size()) {
    fail("trailing junk after number");
  }
  double hz = val * mult;
  if (!std::isfinite(hz)) {
    fail("must be finite");
  }
  if (!(hz > 0.0)) {
    fail("must be positive");
  }
  return hz;
}

struct JittedKernel {
  void* code = nullptr;
};

JittedKernel jit_compile(ferret::Benchmark& b, const ferret::Params& p) {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  if (c == nullptr) {
    return {};
  }
  b.emit_kernel(c, p);
  if (sljit_get_compiler_error(c) != SLJIT_SUCCESS) {
    sljit_free_compiler(c);
    return {};
  }
  void* code = sljit_generate_code(c, 0, nullptr);
  sljit_free_compiler(c);
  return {code};
}

void jit_free(JittedKernel& k) {
  if (k.code != nullptr) {
    sljit_free_code(k.code, nullptr);
  }
  k.code = nullptr;
}

int do_list() {
  for (const auto& n : ferret::BenchmarkRegistry::names()) {
    std::cout << n << "\n";
  }
  return 0;
}

int64_t parse_option_value(const std::string& v) {
  size_t pos = 0;
  int64_t parsed = 0;
  try {
    parsed = std::stoll(v, &pos);
  } catch (const std::exception&) {
    throw std::invalid_argument("not an integer: " + v);
  }
  if (pos != v.size()) {
    throw std::invalid_argument("trailing junk after integer: " + v);
  }
  return parsed;
}

// NOLINTNEXTLINE(readability-function-cognitive-complexity)
int do_run(const std::string& name, const std::map<std::string, std::string>& cli_axis_overrides,
           const std::string& out_path, int core, std::optional<double> freq_hz, int K, int warmup) {
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
        option_values[k] = parse_option_value(v);
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

  // Inject non-swept options into every row so they're recorded in CSV.
  for (auto& p : rows) {
    for (const auto& [k, v] : option_values) {
      p.set(k, v);
    }
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
    JittedKernel kern;
    try {
      size_t pre_iters = bench->iterations(p);
      size_t pre_sites = bench->sites_per_kernel(p);
      if (pre_iters == 0 || pre_sites == 0) {
        flog::error("invalid params: yields zero work (iterations={}, sites_per_kernel={})", pre_iters, pre_sites);
        return 2;
      }

      kern = jit_compile(*bench, p);
      if (kern.code == nullptr) {
        m.jit_failed = true;
        m.iters = pre_iters;
        m.sites = pre_sites;
        flog::warn("sljit_error on params; emitting empty row");
      } else {
        auto fn = reinterpret_cast<void (*)()>(kern.code);
        m = ferret::runner::measure(fn, pre_iters, pre_sites, K, warmup);
        jit_free(kern);
      }
    } catch (const std::exception& e) {
      jit_free(kern);
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
    for (const auto& tok : run_cmd->remaining()) {
      if (tok.size() < 3 || tok[0] != '-' || tok[1] != '-') {
        flog::error("unexpected argument: {}", tok);
        return 2;
      }
      auto eq = tok.find('=');
      if (eq == std::string::npos) {
        flog::error("--axis flags must be --name=value: {}", tok);
        return 2;
      }
      overrides[tok.substr(2, eq - 2)] = tok.substr(eq + 1);
    }

    std::optional<double> freq_hz;
    if (!freq_str.empty()) {
      try {
        freq_hz = parse_freq(freq_str);
      } catch (const std::exception& e) {
        flog::error("invalid {}", e.what());
        return 2;
      }
    }
    return do_run(name, overrides, out_path, core, freq_hz, K, warmup);
  }

  return 0;
}

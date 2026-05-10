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
#include "ferret/pinning.hpp"
#include "ferret/runner.hpp"
#include "ferret/sweep.hpp"
#include "ferret/timing.hpp"

namespace {

double parse_freq(const std::string& s) {
  auto fail = [&](const char* why) {
    throw std::invalid_argument(std::string("--freq: ") + why + ": " + s);
  };

  std::string num = s;
  double mult = 1.0;
  auto strip_suffix = [&](const std::string& suf, double m) {
    if (num.size() >= suf.size() &&
        num.compare(num.size() - suf.size(), suf.size(), suf) == 0) {
      num.resize(num.size() - suf.size());
      mult = m;
      return true;
    }
    return false;
  };
  strip_suffix("GHz", 1e9) || strip_suffix("MHz", 1e6) ||
      strip_suffix("kHz", 1e3) || strip_suffix("Hz", 1.0);

  if (num.empty()) fail("empty numeric component");
  size_t consumed = 0;
  double val = 0.0;
  try {
    val = std::stod(num, &consumed);
  } catch (const std::exception&) {
    fail("not a number");
  }
  if (consumed != num.size()) fail("trailing junk after number");
  double hz = val * mult;
  if (!std::isfinite(hz)) fail("must be finite");
  if (!(hz > 0.0)) fail("must be positive");
  return hz;
}

struct JittedKernel {
  void* code = nullptr;
};

JittedKernel jit_compile(ferret::Benchmark& b, const ferret::Params& p) {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  if (!c) return {};
  b.emit_kernel(c, p);
  if (sljit_get_compiler_error(c) != SLJIT_SUCCESS) {
    sljit_free_compiler(c);
    return {};
  }
  void* code = sljit_generate_code(c, 0, nullptr);
  sljit_free_compiler(c);
  return { code };
}

void jit_free(JittedKernel& k) {
  if (k.code) sljit_free_code(k.code, nullptr);
  k.code = nullptr;
}

int do_list() {
  for (const auto& n : ferret::BenchmarkRegistry::names()) {
    std::cout << n << "\n";
  }
  return 0;
}

int do_run(const std::string& name,
           const std::map<std::string, std::string>& cli_axis_overrides,
           const std::string& out_path,
           int core,
           std::optional<double> freq_hz,
           int K,
           int warmup) {
  auto bench = ferret::BenchmarkRegistry::create(name);
  if (!bench) {
    std::cerr << "ferret: unknown benchmark '" << name
              << "'. Try `ferret list`.\n";
    return 2;
  }

  std::map<std::string, std::vector<int64_t>> overrides;
  ferret::SweepAxes axes = bench->axes();
  for (const auto& [k, v] : cli_axis_overrides) {
    const ferret::Axis* matching = nullptr;
    for (const auto& a : axes) if (a.name() == k) matching = &a;
    if (!matching) {
      std::cerr << "ferret: unknown axis --" << k << " for benchmark "
                << name << "\n";
      return 2;
    }
    try {
      overrides[k] = ferret::parse_cli_axis_value(v, *matching);
    } catch (const std::exception& e) {
      std::cerr << "ferret: invalid value for --" << k << ": "
                << e.what() << "\n";
      return 2;
    }
  }

  std::vector<ferret::Params> rows;
  try {
    rows = ferret::sweep::expand(axes, overrides);
  } catch (const std::exception& e) {
    std::cerr << "ferret: invalid sweep: " << e.what() << "\n";
    return 2;
  }

  if (core >= 0) {
    if (!ferret::pinning::pin_to_core(core))
      std::cerr << "ferret: warning: pin_to_core(" << core << ") failed\n";
  }
  if (!ferret::pinning::boost_priority())
    std::cerr << "ferret: warning: boost_priority failed\n";
  if (!ferret::pinning::lock_memory())
    std::cerr << "ferret: warning: mlockall failed\n";

  std::ofstream ofs;
  std::ostream* out_stream = &std::cout;
  if (!out_path.empty()) {
    ofs.open(out_path);
    if (!ofs) {
      std::cerr << "ferret: cannot open output: " << out_path << "\n";
      return 2;
    }
    out_stream = &ofs;
  }

  std::vector<std::string> axis_cols;
  for (const auto& a : axes) axis_cols.push_back(a.name());

  // tpns is the divisor for every CSV row; non-finite or zero would leak
  // inf/nan into output.
  double tpns = ferret::timing::ticks_per_ns();
  if (!std::isfinite(tpns) || !(tpns > 0.0)) {
    std::cerr << "ferret: ticks_per_ns calibration returned non-finite "
                 "or non-positive value: " << tpns << "\n";
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
        std::cerr << "ferret: invalid params: yields zero work"
                  << " (iterations=" << pre_iters
                  << ", sites_per_kernel=" << pre_sites << ")\n";
        return 2;
      }

      kern = jit_compile(*bench, p);
      if (!kern.code) {
        m.jit_failed = true;
        m.iters = pre_iters;
        m.sites = pre_sites;
        std::cerr << "ferret: sljit_error on params; emitting empty row\n";
      } else {
        auto fn = reinterpret_cast<void (*)(void)>(kern.code);
        m = ferret::runner::measure(fn, pre_iters, pre_sites, K, warmup);
        jit_free(kern);
      }
    } catch (const std::exception& e) {
      jit_free(kern);
      std::cerr << "ferret: benchmark error on params: " << e.what() << "\n";
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

int main(int argc, char** argv) {
  CLI::App app{"ferret — frontend reverse-engineering toolkit"};
  app.require_subcommand(1);

  auto* list_cmd = app.add_subcommand("list", "List registered benchmarks");

  auto* run_cmd = app.add_subcommand("run", "Run a benchmark");
  std::string name;
  run_cmd->add_option("name", name, "benchmark name")->required();

  std::string out_path;
  run_cmd->add_option("--out", out_path, "CSV output path (default stdout)");

  int core = -1;
  run_cmd->add_option("--core", core, "core to pin to (default: no pin)");

  std::string freq_str;
  run_cmd->add_option("--freq", freq_str,
      "core frequency, e.g. 4.521GHz; enables cycles_per_site columns");

  int K = 7;
  run_cmd->add_option("--reps", K,
      "number of timed repetitions per param point (default 7)");

  int warmup = 1;
  run_cmd->add_option("--warmup", warmup,
      "warmup calls before each measurement (default 1)");

  run_cmd->allow_extras();

  CLI11_PARSE(app, argc, argv);

  if (*list_cmd) return do_list();

  if (*run_cmd) {
    if (K < 1) {
      std::cerr << "ferret: --reps must be >= 1 (got " << K << ")\n";
      return 2;
    }
    if (warmup < 0) {
      std::cerr << "ferret: --warmup must be >= 0 (got " << warmup << ")\n";
      return 2;
    }

    std::map<std::string, std::string> overrides;
    for (const auto& tok : run_cmd->remaining()) {
      if (tok.size() < 3 || tok[0] != '-' || tok[1] != '-') {
        std::cerr << "ferret: unexpected argument: " << tok << "\n";
        return 2;
      }
      auto eq = tok.find('=');
      if (eq == std::string::npos) {
        std::cerr << "ferret: --axis flags must be --name=value: "
                  << tok << "\n";
        return 2;
      }
      overrides[tok.substr(2, eq - 2)] = tok.substr(eq + 1);
    }

    std::optional<double> freq_hz;
    if (!freq_str.empty()) {
      try {
        freq_hz = parse_freq(freq_str);
      } catch (const std::exception& e) {
        std::cerr << "ferret: invalid " << e.what() << "\n";
        return 2;
      }
    }
    return do_run(name, overrides, out_path, core, freq_hz, K, warmup);
  }

  return 0;
}

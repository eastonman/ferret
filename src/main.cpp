#include <CLI/CLI.hpp>

#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "ferret/cli_axis.hpp"
#include "ferret/freq.hpp"
#include "ferret/log.hpp"
#include "ferret/run_command.hpp"

// Aliased as `flog` (not `log`) to avoid colliding with the C library
// `::log(double)` declared in <math.h> on glibc, which leaks into the
// global namespace via transitive includes (e.g., <filesystem>).
namespace flog = ferret::log;

int main(int argc, char** argv) {
  try {
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
      return ferret::list_command();
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

      ferret::RunOptions opts;
      try {
        opts.overrides = ferret::parse_extras(run_cmd->remaining());
      } catch (const std::exception& e) {
        flog::error("{}", e.what());
        return 2;
      }

      if (!freq_str.empty()) {
        try {
          opts.freq_hz = ferret::parse_freq(freq_str);
        } catch (const std::exception& e) {
          flog::error("invalid {}", e.what());
          return 2;
        }
      }
      opts.name = name;
      opts.out_path = out_path;
      opts.core = core;
      opts.reps = K;
      opts.warmup = warmup;
      opts.seed = seed;
      return ferret::run(opts);
    }

    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ferret: unexpected exception: " << e.what() << "\n";
    return 1;
  } catch (...) {
    std::cerr << "ferret: unexpected non-standard exception\n";
    return 1;
  }
}

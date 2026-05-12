#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace ferret {

struct RunOptions {
  std::string name;
  std::map<std::string, std::string> overrides;
  std::string out_path;
  int core = -1;
  std::optional<double> freq_hz;
  int reps = 7;
  int warmup = 1;
  int64_t seed = 1;
};

// Runs one benchmark sweep end-to-end. Returns process exit code
// (0 success, 2 user/parameter error). Never throws across the boundary.
int run(const RunOptions& opts);

// Prints registered benchmark names to stdout, one per line. Always returns 0.
int list_command();

}  // namespace ferret

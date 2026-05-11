#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "ferret/params.hpp"
#include "ferret/runner.hpp"

namespace ferret {

class CsvWriter {
 public:
  // freq_hz: when set, the writer emits cycles_per_site_* and freq_hz
  // columns; when unset, only ns_per_site_* is emitted.
  CsvWriter(std::ostream& os, std::string benchmark_name, std::vector<std::string> axis_columns,
            std::optional<double> freq_hz);

  void write_header();
  void write_row(const Params& p, const MeasurementRow& m, double ticks_per_ns);

 private:
  std::ostream& os_;
  std::string benchmark_name_;
  std::vector<std::string> axis_columns_;
  std::optional<double> freq_hz_;
};

}  // namespace ferret

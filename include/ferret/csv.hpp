#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "ferret/params.hpp"
#include "ferret/runner.hpp"

namespace ferret {

// Writes one CSV row per measurement to the supplied ostream. Streaming
// — no internal buffering. Caller owns the ostream lifetime; CsvWriter
// does not flush on destruction.
class CsvWriter {
 public:
  // freq_hz: when set, the writer emits cycles_per_site_* and freq_hz
  // columns; when unset, only ns_per_site_* is emitted.
  CsvWriter(std::ostream& os, std::string benchmark_name, std::vector<std::string> axis_columns,
            std::optional<double> freq_hz);

  // Call exactly once before any write_row. Columns: `benchmark`, the
  // supplied axis columns in order, `ticks_min, ticks_median, iters,
  // sites_per_iter, reps`, `ns_per_site_min, ns_per_site_median`, and —
  // when freq_hz was supplied — `cycles_per_site_min,
  // cycles_per_site_median, freq_hz`.
  void write_header();

  // Emit one row. When MeasurementRow::jit_failed is true, every timing
  // column is empty; the axis columns are still written.
  void write_row(const Params& p, const MeasurementRow& m, double ticks_per_ns);

 private:
  std::ostream& os_;
  std::string benchmark_name_;
  std::vector<std::string> axis_columns_;
  std::optional<double> freq_hz_;
};

}  // namespace ferret

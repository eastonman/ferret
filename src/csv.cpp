#include "ferret/csv.hpp"

#include <iomanip>

namespace ferret {

CsvWriter::CsvWriter(std::ostream& os, std::string benchmark_name, std::vector<std::string> axis_columns,
                     std::optional<double> freq_hz)
    : os_(os), benchmark_name_(std::move(benchmark_name)), axis_columns_(std::move(axis_columns)), freq_hz_(freq_hz) {}

void CsvWriter::write_header() {
  os_ << "benchmark";
  for (const auto& a : axis_columns_) {
    os_ << "," << a;
  }
  os_ << ",ticks_min,ticks_median,iters,sites_per_iter,reps" << ",ns_per_site_min,ns_per_site_median";
  if (freq_hz_) {
    os_ << ",cycles_per_site_min,cycles_per_site_median,freq_hz";
  }
  os_ << "\n";
}

void CsvWriter::write_row(const Params& p, const MeasurementRow& m, double ticks_per_ns) {
  os_ << benchmark_name_;
  for (const auto& a : axis_columns_) {
    os_ << ",";
    if (p.has(a)) {
      os_ << p.get<int64_t>(a);
    }
  }

  if (m.jit_failed) {
    // Empty cells: ticks_min,ticks_median,iters,sites_per_iter,reps,ns_min,ns_med
    os_ << ",,,,,,,";
    if (freq_hz_) {
      os_ << ",,,";
    }
    os_ << "\n";
    return;
  }

  os_ << "," << m.ticks_min << "," << m.ticks_median << "," << m.iters << "," << m.sites << "," << m.reps;

  double ns_per_site_min = static_cast<double>(m.ticks_min) / static_cast<double>(m.iters * m.sites) / ticks_per_ns;
  double ns_per_site_med = static_cast<double>(m.ticks_median) / static_cast<double>(m.iters * m.sites) / ticks_per_ns;

  os_ << "," << std::fixed << std::setprecision(4) << ns_per_site_min << "," << ns_per_site_med;

  if (freq_hz_) {
    double freq_ghz = *freq_hz_ / 1e9;
    os_ << "," << ns_per_site_min * freq_ghz << "," << ns_per_site_med * freq_ghz << "," << std::setprecision(0)
        << *freq_hz_;
  }

  os_ << std::defaultfloat << "\n";
}

}  // namespace ferret

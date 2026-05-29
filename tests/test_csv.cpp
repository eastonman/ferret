#include <gtest/gtest.h>

#include <sstream>

#include "ferret/csv.hpp"
#include "ferret/runner.hpp"

using namespace ferret;

TEST(Csv, HeaderWithoutFreq) {
  std::ostringstream os;
  CsvWriter w(os, "direct_branch_footprint", {"branches", "spacing"}, std::nullopt);
  w.write_header();
  std::string out = os.str();
  EXPECT_NE(out.find("benchmark,branches,spacing,"), std::string::npos);
  EXPECT_NE(out.find("ns_per_site_min"), std::string::npos);
  EXPECT_EQ(out.find("cycles_per_site_min"), std::string::npos);
  EXPECT_EQ(out.find("freq_hz"), std::string::npos);
}

TEST(Csv, HeaderWithFreqIncludesCycleColumns) {
  std::ostringstream os;
  CsvWriter w(os, "direct_branch_footprint", {"branches"}, 4'500'000'000.0);
  w.write_header();
  std::string out = os.str();
  EXPECT_NE(out.find("cycles_per_site_min"), std::string::npos);
  EXPECT_NE(out.find("freq_hz"), std::string::npos);
}

// Authoritative pin of the metadata column names and order emitted by
// CsvWriter. The Python plotter's METADATA_COLS and the run_benchmarks.sh
// validator mirror this; the schema-contract tests enforce the match.
TEST(Csv, MetadataHeaderExactNoFreq) {
  std::ostringstream os;
  CsvWriter w(os, "b", {}, std::nullopt);
  w.write_header();
  EXPECT_EQ(os.str(),
            "benchmark,ticks_min,ticks_median,iters,sites_per_iter,reps,"
            "ns_per_site_min,ns_per_site_median\n");
}

TEST(Csv, MetadataHeaderExactWithFreq) {
  std::ostringstream os;
  CsvWriter w(os, "b", {}, 4.0e9);
  w.write_header();
  EXPECT_EQ(os.str(),
            "benchmark,ticks_min,ticks_median,iters,sites_per_iter,reps,"
            "ns_per_site_min,ns_per_site_median,"
            "cycles_per_site_min,cycles_per_site_median,freq_hz\n");
}

TEST(Csv, RowFormatNoFreq) {
  std::ostringstream os;
  CsvWriter w(os, "bench", {"x"}, std::nullopt);
  w.write_header();
  Params p;
  p.set("x", 42);
  MeasurementRow m;
  m.ticks_min = 1000;
  m.ticks_median = 1100;
  m.iters = 10;
  m.sites = 100;
  m.reps = 5;
  m.jit_failed = false;
  w.write_row(p, m, 2.0);
  std::string out = os.str();
  EXPECT_NE(out.find("bench,42,1000,1100,10,100,5,"), std::string::npos);
}

TEST(Csv, RowJitFailedHasEmptyMetricColumns) {
  std::ostringstream os;
  CsvWriter w(os, "bench", {"x"}, std::nullopt);
  w.write_header();
  Params p;
  p.set("x", 7);
  MeasurementRow m;
  m.jit_failed = true;
  w.write_row(p, m, 2.0);
  std::string out = os.str();
  EXPECT_NE(out.find("bench,7,,,,,,,"), std::string::npos);
}

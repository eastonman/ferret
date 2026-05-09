#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#ifndef FERRET_BINARY
#error "FERRET_BINARY must be defined"
#endif

namespace {

std::string slurp(const std::string& path) {
  std::ifstream f(path);
  std::stringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

int run(const std::string& cmd) {
  return std::system(cmd.c_str());
}

}  // namespace

TEST(Integration, DirectBranchFootprintProducesNonEmptyRows) {
  auto out = std::filesystem::temp_directory_path() / "ferret_btb.csv";
  std::filesystem::remove(out);
  std::string cmd = std::string(FERRET_BINARY) +
      " run direct_branch_footprint"
      " --branches=1,2,4,8 --spacing_bytes=64"
      " --reps=3 --warmup=1"
      " --out=" + out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 5u);
  EXPECT_EQ(contents.find(",,\n"), std::string::npos);
}

TEST(Integration, DependentChainThroughputProducesOneRow) {
  auto out = std::filesystem::temp_directory_path() / "ferret_freq.csv";
  std::filesystem::remove(out);
  std::string cmd = std::string(FERRET_BINARY) +
      " run dependent_chain_throughput"
      " --chain_length=1000000 --reps=3 --warmup=1"
      " --out=" + out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 2u);
}

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#if defined(__unix__) || defined(__APPLE__)
#include <sys/wait.h>
#endif

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

// Process exit code returned by std::system on POSIX is encoded —
// WEXITSTATUS extracts the actual program exit code.
namespace {
int actual_exit_code(int sys_status) {
#if defined(__unix__) || defined(__APPLE__)
  if (WIFEXITED(sys_status)) return WEXITSTATUS(sys_status);
  return -1;
#else
  return sys_status;
#endif
}
}  // namespace

TEST(Integration, InvalidFreqExitsTwoNoCrash) {
  auto err = std::filesystem::temp_directory_path() / "ferret_freq_err.txt";
  std::filesystem::remove(err);
  std::string cmd = std::string(FERRET_BINARY) +
      " run direct_branch_footprint"
      " --branches=1,2 --spacing_bytes=64 --reps=2 --warmup=1"
      " --freq=bogus 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2 (config error), got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos)
      << "stderr did not contain a 'ferret:' message: " << err_contents;
}

TEST(Integration, NegativeRepsExitsTwoNoCrash) {
  auto err = std::filesystem::temp_directory_path() / "ferret_reps_err.txt";
  std::filesystem::remove(err);
  std::string cmd = std::string(FERRET_BINARY) +
      " run direct_branch_footprint"
      " --branches=1,2 --spacing_bytes=64 --reps=0 --warmup=1"
      " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
}

TEST(Integration, ZeroBranchesExitsTwoNoCrash) {
  auto err = std::filesystem::temp_directory_path() / "ferret_branches0_err.txt";
  std::filesystem::remove(err);
  std::string cmd = std::string(FERRET_BINARY) +
      " run direct_branch_footprint"
      " --branches=0 --spacing_bytes=64 --reps=2 --warmup=1"
      " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
}

TEST(Integration, NegativeBranchesExitsTwoNoCrash) {
  auto err = std::filesystem::temp_directory_path() / "ferret_branchesNeg_err.txt";
  std::filesystem::remove(err);
  std::string cmd = std::string(FERRET_BINARY) +
      " run direct_branch_footprint"
      " --branches=-1 --spacing_bytes=64 --reps=2 --warmup=1"
      " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
}

TEST(Integration, HugeBranchesExitsTwoNoCrash) {
  // Extreme positive value: log2 axis accepts it (positive), but the
  // benchmark allocator throws std::length_error / std::bad_alloc.
  // do_run must catch and translate to exit 2.
  auto err = std::filesystem::temp_directory_path() / "ferret_branchesHuge_err.txt";
  std::filesystem::remove(err);
  std::string cmd = std::string(FERRET_BINARY) +
      " run direct_branch_footprint"
      " --branches=4611686018427387904 --spacing_bytes=64 --reps=2 --warmup=1"
      " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
}

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
  // do_run must catch and translate to exit 2 AND must not have leaked
  // a CSV header to stdout (spec §7 class-1: no partial output).
  auto out = std::filesystem::temp_directory_path() / "ferret_branchesHuge_out.txt";
  auto err = std::filesystem::temp_directory_path() / "ferret_branchesHuge_err.txt";
  std::filesystem::remove(out);
  std::filesystem::remove(err);
  std::string cmd = std::string(FERRET_BINARY) +
      " run direct_branch_footprint"
      " --branches=4611686018427387904 --spacing_bytes=64 --reps=2 --warmup=1"
      " > " + out.string() + " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  std::string out_contents = slurp(out.string());
  EXPECT_TRUE(out_contents.empty())
      << "expected stdout to be empty (no partial CSV), got: " << out_contents;
}

TEST(Integration, MixedSweepHugeRowExitsTwoNoPartialOutput) {
  // First sweep value succeeds; second is a huge log2 value that throws
  // inside emit_kernel. Buffer-then-flush semantics require the output
  // file/stdout to stay empty even though earlier rows succeeded.
  auto out = std::filesystem::temp_directory_path() / "ferret_mix_out.txt";
  auto err = std::filesystem::temp_directory_path() / "ferret_mix_err.txt";
  std::filesystem::remove(out);
  std::filesystem::remove(err);
  std::string cmd = std::string(FERRET_BINARY) +
      " run direct_branch_footprint"
      " --branches=1,4611686018427387904 --spacing_bytes=64 --reps=2 --warmup=1"
      " > " + out.string() + " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  std::string out_contents = slurp(out.string());
  EXPECT_TRUE(out_contents.empty())
      << "expected stdout empty (no partial CSV from earlier successful row), got: "
      << out_contents;
}

TEST(Integration, MixedSweepHugeRowOutFileStaysEmpty) {
  // Same scenario but with --out=PATH. The file must end empty so the
  // user doesn't get a misleading partial CSV.
  auto out = std::filesystem::temp_directory_path() / "ferret_mix_outfile.csv";
  auto err = std::filesystem::temp_directory_path() / "ferret_mix_outfile_err.txt";
  std::filesystem::remove(out);
  std::filesystem::remove(err);
  std::string cmd = std::string(FERRET_BINARY) +
      " run direct_branch_footprint"
      " --branches=1,4611686018427387904 --spacing_bytes=64 --reps=2 --warmup=1"
      " --out=" + out.string() +
      " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  ASSERT_TRUE(std::filesystem::exists(out));
  EXPECT_EQ(0u, std::filesystem::file_size(out))
      << "expected output file empty (no partial CSV)";
}

TEST(Integration, FreqInfExitsTwoNoCrash) {
  auto err = std::filesystem::temp_directory_path() / "ferret_freqInf_err.txt";
  std::filesystem::remove(err);
  std::string cmd = std::string(FERRET_BINARY) +
      " run direct_branch_footprint"
      " --branches=1,2 --spacing_bytes=64 --reps=2 --warmup=1"
      " --freq=inf 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
}

TEST(Integration, FreqNanExitsTwoNoCrash) {
  auto err = std::filesystem::temp_directory_path() / "ferret_freqNan_err.txt";
  std::filesystem::remove(err);
  std::string cmd = std::string(FERRET_BINARY) +
      " run direct_branch_footprint"
      " --branches=1,2 --spacing_bytes=64 --reps=2 --warmup=1"
      " --freq=nan 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
}

TEST(Integration, NegativeChainLengthExitsTwoNoCrash) {
  // chain_length=-1 was previously cast through size_t to a huge value,
  // making the kernel loop run effectively forever. Params::get<size_t>
  // now rejects negatives; do_run translates the throw into exit 2.
  // The test enforces a 5-second wall timeout on the spawned process so
  // a regression manifests as a CTest timeout rather than a hang.
  auto out = std::filesystem::temp_directory_path() / "ferret_chainNeg.csv";
  auto err = std::filesystem::temp_directory_path() / "ferret_chainNeg_err.txt";
  std::filesystem::remove(out);
  std::filesystem::remove(err);
  std::string cmd = std::string("timeout 5 ") + FERRET_BINARY +
      " run dependent_chain_throughput"
      " --chain_length=-1 --reps=2 --warmup=1"
      " --out=" + out.string() +
      " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  ASSERT_TRUE(std::filesystem::exists(out));
  EXPECT_EQ(0u, std::filesystem::file_size(out));
}

TEST(Integration, NegativeSpacingBytesExitsTwoNoCrash) {
  // spacing_bytes=-1 cast to size_t made emit_nops loop SIZE_MAX times.
  // Now Params::get<size_t> rejects the negative value.
  auto out = std::filesystem::temp_directory_path() / "ferret_spacingNeg.csv";
  auto err = std::filesystem::temp_directory_path() / "ferret_spacingNeg_err.txt";
  std::filesystem::remove(out);
  std::filesystem::remove(err);
  std::string cmd = std::string("timeout 5 ") + FERRET_BINARY +
      " run direct_branch_footprint"
      " --branches=1,2 --spacing_bytes=-1 --reps=2 --warmup=1"
      " --out=" + out.string() +
      " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  ASSERT_TRUE(std::filesystem::exists(out));
  EXPECT_EQ(0u, std::filesystem::file_size(out));
}

TEST(Integration, ZeroChainLengthExitsTwoNoCrash) {
  // chain_length=0 yields sites_per_kernel=0, which would divide by
  // zero in CSV normalization. do_run must reject the param point
  // pre-flight with exit 2 and leave the output file empty.
  auto out = std::filesystem::temp_directory_path() / "ferret_chain0.csv";
  auto err = std::filesystem::temp_directory_path() / "ferret_chain0_err.txt";
  std::filesystem::remove(out);
  std::filesystem::remove(err);
  std::string cmd = std::string(FERRET_BINARY) +
      " run dependent_chain_throughput"
      " --chain_length=0 --reps=3 --warmup=1"
      " --out=" + out.string() +
      " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  ASSERT_TRUE(std::filesystem::exists(out));
  EXPECT_EQ(0u, std::filesystem::file_size(out))
      << "expected output file empty (zero-work param rejected)";
}

// Sanity: a non-1024-multiple chain_length still produces a measurement
// with ns_per_site_min in roughly the same ballpark as a 1024-aligned
// run on the same host. Catches regressions where the tail-emission
// code drops or duplicates ops.
TEST(Integration, FreqProbeExactOpCountSanity) {
  auto out_a = std::filesystem::temp_directory_path() / "ferret_freq_a.csv";
  auto out_b = std::filesystem::temp_directory_path() / "ferret_freq_b.csv";
  std::filesystem::remove(out_a);
  std::filesystem::remove(out_b);
  std::string cmd_a = std::string(FERRET_BINARY) +
      " run dependent_chain_throughput"
      " --chain_length=1024 --reps=5 --warmup=2"
      " --out=" + out_a.string();
  std::string cmd_b = std::string(FERRET_BINARY) +
      " run dependent_chain_throughput"
      " --chain_length=1000 --reps=5 --warmup=2"
      " --out=" + out_b.string();
  ASSERT_EQ(0, run(cmd_a));
  ASSERT_EQ(0, run(cmd_b));

  // Pull ns_per_site_min from each CSV's only data row.
  auto extract_ns = [](const std::string& path) -> double {
    std::ifstream f(path);
    std::string header, row;
    std::getline(f, header);
    std::getline(f, row);
    // Find the ns_per_site_min column index from the header.
    std::vector<std::string> cols;
    std::stringstream hs(header);
    std::string tok;
    while (std::getline(hs, tok, ',')) cols.push_back(tok);
    size_t target = 0;
    for (; target < cols.size(); ++target)
      if (cols[target] == "ns_per_site_min") break;
    EXPECT_LT(target, cols.size());
    std::stringstream rs(row);
    std::string val;
    for (size_t i = 0; std::getline(rs, val, ','); ++i) {
      if (i == target) return std::stod(val);
    }
    return -1.0;
  };

  double ns_a = extract_ns(out_a.string());
  double ns_b = extract_ns(out_b.string());
  ASSERT_GT(ns_a, 0.0);
  ASSERT_GT(ns_b, 0.0);
  // Very small chain (1024 ops) is dominated by entry/exit overhead, so
  // exact equality is unrealistic — but the two values should be in the
  // same order of magnitude (both ≈ ns/cycle on the host core).
  double ratio = ns_b / ns_a;
  EXPECT_GT(ratio, 0.3) << "ns_a=" << ns_a << " ns_b=" << ns_b;
  EXPECT_LT(ratio, 3.0) << "ns_a=" << ns_a << " ns_b=" << ns_b;
}

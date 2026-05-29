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

int run(const std::string& cmd) { return std::system(cmd.c_str()); }

// Removes a filesystem path on construction (pre-test cleanup) and again
// on destruction (post-test cleanup, including on assertion failure).
struct TempFileGuard {
  std::filesystem::path path;
  explicit TempFileGuard(std::filesystem::path p) : path(std::move(p)) { std::filesystem::remove(path); }
  ~TempFileGuard() { std::filesystem::remove(path); }
  TempFileGuard(const TempFileGuard&) = delete;
  TempFileGuard& operator=(const TempFileGuard&) = delete;
};

}  // namespace

TEST(Integration, DirectBranchFootprintProducesNonEmptyRows) {
  auto out = std::filesystem::temp_directory_path() / "ferret_btb.csv";
  TempFileGuard guard_out{out};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=1,2,4,8 --spacing_bytes=64"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 5u);
  EXPECT_EQ(contents.find(",,\n"), std::string::npos);
}

TEST(Integration, DirectBranchFootprintSattoloPermuteHeaderAndRows) {
  auto out = std::filesystem::temp_directory_path() / "ferret_btb_sattolo.csv";
  TempFileGuard guard_out{out};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=1,2,4,8 --spacing_bytes=64"
                    " --sattolo_permute=1 --seed=42"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  // Option + seed appear as columns and every data row is non-empty.
  EXPECT_NE(contents.find("sattolo_permute"), std::string::npos);
  EXPECT_NE(contents.find("seed"), std::string::npos);
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 5u);
  EXPECT_EQ(contents.find(",,"), std::string::npos);
}

TEST(Integration, DirectBranchFootprintGeomRangeNonPow2Sweep) {
  auto out = std::filesystem::temp_directory_path() / "ferret_btb_geom.csv";
  TempFileGuard guard_out{out};
  // Branch range kept small. Larger ranges (e.g., 1024..4096@4) blow up
  // sljit on x86_64 because emit_nops emits one op_custom per byte of
  // padding — at spacing=64 that is 59 ops per site, and the per-kernel
  // op count grows past what sljit's metadata tracking handles. The
  // values picked here exercise non-pow2 expansion and the CLI @k path
  // end-to-end without tripping the underlying limit.
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=64..256@2 --spacing_bytes=64"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  // Header + 5 data rows: expand_geom_range(64, 256, 2) lands on
  // {64, 91, 128, 181, 256}; hi=256 is on the natural sequence so no
  // hi-forcing.
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 6u);
  // No empty cells.
  EXPECT_EQ(contents.find(",,"), std::string::npos);
  // Spot-check two non-pow2 values appear as row substrings of the form
  // ",<branches>,<spacing_bytes>," — column 2 is branches, column 3
  // spacing_bytes by ferret convention (first axis is slowest-varying).
  EXPECT_NE(contents.find(",91,64,"), std::string::npos);
  EXPECT_NE(contents.find(",181,64,"), std::string::npos);
}

TEST(Integration, DependentChainThroughputProducesOneRow) {
  auto out = std::filesystem::temp_directory_path() / "ferret_freq.csv";
  TempFileGuard guard_out{out};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run dependent_chain_throughput"
                    " --chain_length=1000000 --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 2u);
}

TEST(Integration, BranchHistoryFootprintProducesExpectedRowCount) {
  auto out = std::filesystem::temp_directory_path() / "ferret_bhf.csv";
  TempFileGuard guard_out{out};
  // branches ∈ {1,2,3,4} × history_len ∈ {4,6,8} → 12 data rows.
  std::string cmd = std::string(FERRET_BINARY) +
                    " run branch_history_footprint"
                    " --branches=1..4 --history_len=4..8"
                    " --pattern=0"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 13u);  // header + 12 data rows
  EXPECT_EQ(contents.find(",,\n"), std::string::npos);
}

TEST(Integration, BranchHistoryFootprintHeaderHasExpectedColumns) {
  auto out = std::filesystem::temp_directory_path() / "ferret_bhf_hdr.csv";
  TempFileGuard guard_out{out};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run branch_history_footprint"
                    " --branches=1 --history_len=4"
                    " --pattern=0 --reps=2 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  EXPECT_NE(contents.find("branches"), std::string::npos);
  EXPECT_NE(contents.find("history_len"), std::string::npos);
  EXPECT_NE(contents.find("pattern"), std::string::npos);
  EXPECT_NE(contents.find("spacing_bytes"), std::string::npos);
}

TEST(Integration, BranchHistoryFootprintRandomPatternProducesNonEmptyRows) {
  auto out = std::filesystem::temp_directory_path() / "ferret_bhf_rand.csv";
  TempFileGuard guard_out{out};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run branch_history_footprint"
                    " --branches=1,2 --history_len=4,8"
                    " --pattern=1 --seed=7"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 5u);  // header + 4 rows
  EXPECT_EQ(contents.find(",,"), std::string::npos);
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

TEST(Integration, UnknownOptionRejected) {
  auto err = std::filesystem::temp_directory_path() / "ferret_unknown_opt_err.txt";
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=1 --spacing_bytes=64 --reps=2 --warmup=1"
                    " --no_such_thing=1 2> " +
                    err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
}

TEST(Integration, InvalidFreqExitsTwoNoCrash) {
  auto err = std::filesystem::temp_directory_path() / "ferret_freq_err.txt";
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=1,2 --spacing_bytes=64 --reps=2 --warmup=1"
                    " --freq=bogus 2> " +
                    err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2 (config error), got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos)
      << "stderr did not contain a 'ferret:' message: " << err_contents;
}

TEST(Integration, NegativeRepsExitsTwoNoCrash) {
  auto err = std::filesystem::temp_directory_path() / "ferret_reps_err.txt";
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=1,2 --spacing_bytes=64 --reps=0 --warmup=1"
                    " 2> " +
                    err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
}

TEST(Integration, ZeroBranchesExitsTwoNoCrash) {
  auto err = std::filesystem::temp_directory_path() / "ferret_branches0_err.txt";
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=0 --spacing_bytes=64 --reps=2 --warmup=1"
                    " 2> " +
                    err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
}

TEST(Integration, NegativeBranchesExitsTwoNoCrash) {
  auto err = std::filesystem::temp_directory_path() / "ferret_branchesNeg_err.txt";
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=-1 --spacing_bytes=64 --reps=2 --warmup=1"
                    " 2> " +
                    err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
}

TEST(Integration, HugeBranchesEmitsEmptyRowNoCrash) {
  // Extreme positive value: log2 axis accepts it (positive), but the
  // benchmark allocator throws std::length_error / std::bad_alloc.
  // ferret::run records the point as an empty row so full-sweep
  // artifacts remain usable even when one generated kernel is too big
  // for the host.
  auto out = std::filesystem::temp_directory_path() / "ferret_branchesHuge_out.txt";
  auto err = std::filesystem::temp_directory_path() / "ferret_branchesHuge_err.txt";
  TempFileGuard guard_out{out};
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=4611686018427387904 --spacing_bytes=64 --reps=2 --warmup=1"
                    " > " +
                    out.string() + " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 0) << "expected exit 0, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  std::string out_contents = slurp(out.string());
  size_t newlines = std::count(out_contents.begin(), out_contents.end(), '\n');
  EXPECT_EQ(newlines, 2u);
  EXPECT_NE(out_contents.find("4611686018427387904,64,0,1,,,,,,,"), std::string::npos);
}

TEST(Integration, MixedSweepHugeRowEmitsEmptyRow) {
  // First sweep value succeeds; second is huge enough to fail during
  // codegen/resource allocation. Full benchmark sweeps should keep the
  // artifact usable by recording that failed point as an empty row.
  auto out = std::filesystem::temp_directory_path() / "ferret_mix_out.txt";
  auto err = std::filesystem::temp_directory_path() / "ferret_mix_err.txt";
  TempFileGuard guard_out{out};
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=1,4611686018427387904 --spacing_bytes=64 --reps=2 --warmup=1"
                    " > " +
                    out.string() + " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 0) << "expected exit 0, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  std::string out_contents = slurp(out.string());
  size_t newlines = std::count(out_contents.begin(), out_contents.end(), '\n');
  EXPECT_EQ(newlines, 3u);  // header + successful row + empty failure row
  EXPECT_NE(out_contents.find("4611686018427387904,64,0,1,,,,,,,"), std::string::npos);
}

TEST(Integration, MixedSweepHugeRowOutFileKeepsEmptyRow) {
  // Same scenario but with --out=PATH.
  auto out = std::filesystem::temp_directory_path() / "ferret_mix_outfile.csv";
  auto err = std::filesystem::temp_directory_path() / "ferret_mix_outfile_err.txt";
  TempFileGuard guard_out{out};
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=1,4611686018427387904 --spacing_bytes=64 --reps=2 --warmup=1"
                    " --out=" +
                    out.string() + " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 0) << "expected exit 0, got " << rc;
  ASSERT_TRUE(std::filesystem::exists(out));
  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 3u);
  EXPECT_NE(contents.find("4611686018427387904,64,0,1,,,,,,,"), std::string::npos);
}

TEST(Integration, FreqInfExitsTwoNoCrash) {
  auto err = std::filesystem::temp_directory_path() / "ferret_freqInf_err.txt";
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=1,2 --spacing_bytes=64 --reps=2 --warmup=1"
                    " --freq=inf 2> " +
                    err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
}

TEST(Integration, FreqNanExitsTwoNoCrash) {
  auto err = std::filesystem::temp_directory_path() / "ferret_freqNan_err.txt";
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=1,2 --spacing_bytes=64 --reps=2 --warmup=1"
                    " --freq=nan 2> " +
                    err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
}

TEST(Integration, NegativeChainLengthExitsTwoNoCrash) {
  // chain_length=-1 must exit 2: Params::get<size_t> rejects negatives
  // (otherwise the size_t conversion would yield a huge loop count)
  // and ferret::run translates the throw into exit 2.
  // CTest enforces a per-test wall-clock timeout (set in
  // tests/CMakeLists.txt via gtest_discover_tests TIMEOUT property) so
  // a regression manifests as a CTest timeout rather than a hang —
  // without taking a runtime dependency on a `timeout` binary.
  auto out = std::filesystem::temp_directory_path() / "ferret_chainNeg.csv";
  auto err = std::filesystem::temp_directory_path() / "ferret_chainNeg_err.txt";
  TempFileGuard guard_out{out};
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run dependent_chain_throughput"
                    " --chain_length=-1 --reps=2 --warmup=1"
                    " --out=" +
                    out.string() + " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  ASSERT_TRUE(std::filesystem::exists(out));
  EXPECT_EQ(0u, std::filesystem::file_size(out));
}

TEST(Integration, NestedCallDepthDepth1SmokeProducesOneRow) {
  auto out = std::filesystem::temp_directory_path() / "ferret_ncd_smoke.csv";
  TempFileGuard guard_out{out};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run nested_call_depth"
                    " --depth=1 --path_table_rows=16"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 2u) << "expected 1 header + 1 data row, got:\n" << contents;
}

namespace {

void expect_spacing_rejected(const std::string& spacing_arg) {
  auto out = std::filesystem::temp_directory_path() / ("ferret_spacing_" + spacing_arg + ".csv");
  auto err = std::filesystem::temp_directory_path() / ("ferret_spacing_" + spacing_arg + "_err.txt");
  TempFileGuard guard_out{out};
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=1,2 --spacing_bytes=" +
                    spacing_arg +
                    " --reps=2 --warmup=1"
                    " --out=" +
                    out.string() + " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "spacing=" << spacing_arg << ": expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos)
      << "spacing=" << spacing_arg << ": stderr=" << err_contents;
  EXPECT_TRUE(!std::filesystem::exists(out) || std::filesystem::file_size(out) == 0u)
      << "spacing=" << spacing_arg << ": partial CSV emitted";
}

}  // namespace

TEST(Integration, NestedCallDepthKEightStaticStillRunsCleanly) {
  // Same shape as the depth-1 smoke, but uses larger depths and asserts
  // no empty cells. Each body emits 8 call sites; this test guards
  // against a regression where some sites fall through or are not
  // properly wired.
  auto out = std::filesystem::temp_directory_path() / "ferret_ncd_k8.csv";
  TempFileGuard guard_out{out};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run nested_call_depth"
                    " --depth=1,4,16 --path_table_rows=16"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));
  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 4u);
  EXPECT_EQ(contents.find(",,"), std::string::npos);
}

TEST(Integration, NestedCallDepthSweepProducesMonotonicCost) {
  auto out = std::filesystem::temp_directory_path() / "ferret_ncd_sweep.csv";
  TempFileGuard guard_out{out};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run nested_call_depth"
                    " --depth=1,2,4,8 --path_table_rows=16"
                    " --reps=5 --warmup=2"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));

  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 5u);
  EXPECT_EQ(contents.find(",,"), std::string::npos);
}

#if defined(__aarch64__) || defined(_M_ARM64)
// AArch64 instructions are 4-byte fixed-width and 4-byte aligned, so the
// per-branch stride must be a multiple of 4. spacing=5 violates that.
TEST(Integration, AArch64SpacingNotMultipleOfFourExitsTwoNoCrash) { expect_spacing_rejected("5"); }

// spacing=2 cannot hold a 4-byte AArch64 branch and also fails the
// multiple-of-4 alignment requirement — either rejection mechanism is
// sufficient; the kernel must not run.
TEST(Integration, AArch64SpacingLessThanFourExitsTwoNoCrash) { expect_spacing_rejected("2"); }
#endif

#if defined(__x86_64__) || defined(_M_X64)
// x86_64 has no alignment requirement, but spacing must still hold one
// branch instruction. The smallest sljit SLJIT_JUMP encoding is a
// 2-byte rel8 short jump, so spacing=1 cannot represent a valid layout
// and must be rejected.
TEST(Integration, X86SpacingSmallerThanBranchExitsTwoNoCrash) { expect_spacing_rejected("1"); }
#endif

TEST(Integration, NegativeSpacingBytesExitsTwoNoCrash) {
  // spacing_bytes=-1 cast to size_t made emit_nops loop SIZE_MAX times.
  // The log2_range axis policy rejects negative values at CLI parse,
  // before any output file is opened. CTest TIMEOUT (tests/CMakeLists.txt)
  // catches any regression that hangs instead of exiting.
  auto out = std::filesystem::temp_directory_path() / "ferret_spacingNeg.csv";
  auto err = std::filesystem::temp_directory_path() / "ferret_spacingNeg_err.txt";
  TempFileGuard guard_out{out};
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run direct_branch_footprint"
                    " --branches=1,2 --spacing_bytes=-1 --reps=2 --warmup=1"
                    " --out=" +
                    out.string() + " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  // No partial output: either the file was not created at all, or it
  // was opened and left empty.
  EXPECT_TRUE(!std::filesystem::exists(out) || std::filesystem::file_size(out) == 0u);
}

TEST(Integration, ZeroChainLengthExitsTwoNoCrash) {
  // chain_length=0 yields sites_per_kernel=0, which would divide by
  // zero in CSV normalization. ferret::run must reject the param point
  // pre-flight with exit 2 and leave the output file empty.
  auto out = std::filesystem::temp_directory_path() / "ferret_chain0.csv";
  auto err = std::filesystem::temp_directory_path() / "ferret_chain0_err.txt";
  TempFileGuard guard_out{out};
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run dependent_chain_throughput"
                    " --chain_length=0 --reps=3 --warmup=1"
                    " --out=" +
                    out.string() + " 2> " + err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2) << "expected exit 2, got " << rc;
  std::string err_contents = slurp(err.string());
  EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  ASSERT_TRUE(std::filesystem::exists(out));
  EXPECT_EQ(0u, std::filesystem::file_size(out)) << "expected output file empty (zero-work param rejected)";
}

// Sanity: a non-1024-multiple chain_length still produces a measurement
// with ns_per_site_min in roughly the same ballpark as a 1024-aligned
// run on the same host. Catches regressions where the tail-emission
// code drops or duplicates ops.
TEST(Integration, FreqProbeExactOpCountSanity) {
  auto out_a = std::filesystem::temp_directory_path() / "ferret_freq_a.csv";
  auto out_b = std::filesystem::temp_directory_path() / "ferret_freq_b.csv";
  TempFileGuard guard_a{out_a};
  TempFileGuard guard_b{out_b};
  std::string cmd_a = std::string(FERRET_BINARY) +
                      " run dependent_chain_throughput"
                      " --chain_length=1024 --reps=5 --warmup=2"
                      " --out=" +
                      out_a.string();
  std::string cmd_b = std::string(FERRET_BINARY) +
                      " run dependent_chain_throughput"
                      " --chain_length=1000 --reps=5 --warmup=2"
                      " --out=" +
                      out_b.string();
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
  // Wider bounds (0.1 .. 10.0) tolerate shared-runner timing noise where
  // overhead-dominated short runs amplify run-to-run variance.
  double ratio = ns_b / ns_a;
  EXPECT_GT(ratio, 0.1) << "ns_a=" << ns_a << " ns_b=" << ns_b;
  EXPECT_LT(ratio, 10.0) << "ns_a=" << ns_a << " ns_b=" << ns_b;
}

TEST(Integration, NestedCallDepthRejectsBadPathTableRows) {
  // path_table_rows is only validated for variant=2 (the path-table kernel).
  // variants 0/1 ignore it, so we explicitly select variant 2 here.
  for (const char* val : {"3", "5", "0", "1"}) {
    auto err = std::filesystem::temp_directory_path() / ("ferret_ncd_bad_rows_" + std::string(val) + ".txt");
    TempFileGuard guard_err{err};
    std::string cmd = std::string(FERRET_BINARY) +
                      " run nested_call_depth"
                      " --depth=2 --variant=2 --path_table_rows=" +
                      val +
                      " --reps=2 --warmup=1"
                      " 2> " +
                      err.string();
    int rc = actual_exit_code(std::system(cmd.c_str()));
    EXPECT_EQ(rc, 2) << "path_table_rows=" << val << ": expected exit 2";
    std::string err_contents = slurp(err.string());
    EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  }
}

TEST(Integration, NestedCallDepthRejectsInvalidVariant) {
  for (const char* val : {"-1", "3", "9"}) {
    auto err = std::filesystem::temp_directory_path() / ("ferret_ncd_bad_variant_" + std::string(val) + ".txt");
    TempFileGuard guard_err{err};
    std::string cmd = std::string(FERRET_BINARY) +
                      " run nested_call_depth"
                      " --depth=2 --variant=" +
                      val +
                      " --reps=2 --warmup=1"
                      " 2> " +
                      err.string();
    int rc = actual_exit_code(std::system(cmd.c_str()));
    EXPECT_EQ(rc, 2) << "variant=" << val << ": expected exit 2";
    std::string err_contents = slurp(err.string());
    EXPECT_NE(err_contents.find("ferret:"), std::string::npos);
  }
}

TEST(Integration, NestedCallDepthAllVariantsProduceRows) {
  // Sweeps depth × variant in one ferret invocation — 4 depths × 3 variants
  // = 12 data rows plus 1 header. Also exercises the variant axis's list
  // syntax so a regression in CLI parsing fails this test.
  auto out = std::filesystem::temp_directory_path() / "ferret_ncd_all_variants.csv";
  TempFileGuard guard_out{out};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run nested_call_depth"
                    " --depth=1,2,4,8 --variant=0,1,2"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));
  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 13u);
  EXPECT_EQ(contents.find(",,"), std::string::npos);
}

TEST(Integration, NestedCallDepthRejectsZeroDepth) {
  auto err = std::filesystem::temp_directory_path() / "ferret_ncd_zero_depth.txt";
  TempFileGuard guard_err{err};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run nested_call_depth"
                    " --depth=0 --path_table_rows=16"
                    " --reps=2 --warmup=1"
                    " 2> " +
                    err.string();
  int rc = actual_exit_code(std::system(cmd.c_str()));
  EXPECT_EQ(rc, 2);
}

TEST(Integration, NestedCallDepthLongSweepRowCount) {
  // 64 swept depths × 1 row each + 1 header = 65 newlines.
  auto out = std::filesystem::temp_directory_path() / "ferret_ncd_full.csv";
  TempFileGuard guard_out{out};
  std::string cmd = std::string(FERRET_BINARY) +
                    " run nested_call_depth"
                    " --depth=1..64 --path_table_rows=16"
                    " --reps=3 --warmup=1"
                    " --out=" +
                    out.string();
  ASSERT_EQ(0, run(cmd));
  std::string contents = slurp(out.string());
  size_t newlines = std::count(contents.begin(), contents.end(), '\n');
  EXPECT_EQ(newlines, 65u) << "expected 1 header + 64 data rows";
  EXPECT_EQ(contents.find(",,"), std::string::npos);
}

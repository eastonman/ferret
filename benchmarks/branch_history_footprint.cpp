extern "C" {
#include <sljitLir.h>
}

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "ferret/bench_helpers.hpp"
#include "ferret/benchmark.hpp"
#include "ferret/padding.hpp"
#include "ferret/permute.hpp"

namespace ferret {

struct BranchHistoryFootprint;  // forward decl for internal namespace ref

namespace {

constexpr size_t kOpBudget = 10'000'000;

// Floor on the bytes a single site (load + cmp-and-branch) can occupy
// across all sljit encodings on this arch. Used to pick the NOP count
// per site as `spacing - kMinSiteBytes` so the chain stride is always
// >= spacing, regardless of which encoding sljit picks. Sites may be
// larger than `spacing` when sljit chooses longer encodings; that's
// fine — the experimental contract is "loose minimum spacing to avoid
// BTB conflict miss," not byte-exact stride.
//
// AArch64: ldr (4) + worst-case single-insn cmp-and-branch (e.g. CBNZ,
//   if sljit's compare-against-zero optimizer kicks in) = 8 bytes.
// x86_64: mov r32,[r32] with mod=00 disp0 = 2 bytes (smallest encoding
//   when sljit picks mod=00 for j=0); test reg,reg = 2 bytes;
//   jne rel8 = 2 bytes. Total = 6 bytes.
#if defined(__aarch64__) || defined(_M_ARM64)
constexpr size_t kMinSiteBytes = 8;
#elif defined(__x86_64__) || defined(_M_X64)
constexpr size_t kMinSiteBytes = 6;
#else
#error "ferret supports only x86_64 and aarch64"
#endif

}  // namespace

namespace branch_history_footprint_internal {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t history_len, int64_t pattern, uint64_t seed) {
  std::vector<uint32_t> flat(branches * history_len, 0U);
  if (pattern == 0) {
    return flat;
  }
  // Mix seed with (branches, history_len) so distinct grid points get
  // distinct fills.
  uint64_t mixed = mix_seed(seed, branches, history_len);
  std::mt19937_64 rng(mixed);
  for (auto& v : flat) {
    v = static_cast<uint32_t>(rng() & 1U);
  }
  return flat;
}

struct LayoutSnapshot {
  std::vector<sljit_label*> labels;
  size_t branches;
  size_t spacing;
};

namespace {
// Set by emit_kernel as a side effect so tests can inspect the most
// recent post-codegen layout. Anonymous namespace → TU-local.
const BranchHistoryFootprint* g_last_emitted = nullptr;
}  // namespace

LayoutSnapshot last_layout_snapshot();  // defined after struct below

}  // namespace branch_history_footprint_internal

struct BranchHistoryFootprint : Benchmark {
  // Per-emission state, lives across emit_kernel → verify_layout for
  // one parameter point. Reused on subsequent points after resize().
  std::vector<uint32_t> flat_;
  std::vector<sljit_label*> last_labels_;
  size_t last_branches_ = 0;
  size_t last_spacing_ = 0;

  [[nodiscard]] std::string name() const override { return "branch_history_footprint"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::geom_range("branches", 1, 1 << 9, /*samples_per_octave=*/1),
        Axis::geom_range("history_len", 4, 1 << 12, /*samples_per_octave=*/1),
    };
  }

  [[nodiscard]] BenchOptions options() const override {
    return {
        BenchOption{.name = "pattern", .default_value = 1},
        BenchOption{.name = "spacing_bytes", .default_value = 16},
    };
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("branches"); }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return compute_iterations(kOpBudget, p.get<size_t>("branches"));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override;
  void verify_layout(sljit_compiler* c) override;
};

void BranchHistoryFootprint::emit_kernel(sljit_compiler* c, const Params& p) {
  auto branches = p.get<size_t>("branches");
  auto history_len = p.get<size_t>("history_len");
  auto pattern = p.get<int64_t>("pattern");
  auto spacing = p.get<size_t>("spacing_bytes");
  auto seed = static_cast<uint64_t>(p.get<int64_t>("seed"));

  // Pre-codegen validation (must throw before mutating `c`).
  // `branches` check runs before iterations() below — x86_64 traps on
  // the 10_000_000 / branches divide-by-zero.
  if (branches < 1) {
    throw std::invalid_argument("branches must be >= 1");
  }
  if (history_len < 1) {
    throw std::invalid_argument("history_len must be >= 1");
  }
  if (pattern != 0 && pattern != 1) {
    throw std::invalid_argument("pattern must be 0 (zero) or 1 (random); got " + std::to_string(pattern));
  }
  if (spacing < kMinSiteBytes) {
    throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                " is smaller than the minimum site encoding (" + std::to_string(kMinSiteBytes) +
                                " bytes) on this architecture");
  }

  size_t iters = iterations(p);

  flat_ = branch_history_footprint_internal::generate_pattern_fill(branches, history_len, pattern, seed);

  // sljit prologue: 3 scratches + 2 saveds.
  // SLJIT_S0 = flat_base, SLJIT_S1 = hist_idx, SLJIT_R0 = iter counter.
  // SLJIT_R1 = row_ptr (recomputed per outer iter), SLJIT_R2 = loaded value.
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/3, /*saved=*/2, /*local_size=*/0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_S0, 0, SLJIT_IMM, reinterpret_cast<sljit_sw>(flat_.data()));
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 0);

  emit_outer_loop(c, SLJIT_R0, iters, [&] {
    // row_ptr = flat_base + hist_idx * (branches * 4)
    sljit_emit_op2(c, SLJIT_MUL, SLJIT_R1, 0, SLJIT_S1, 0, SLJIT_IMM, static_cast<sljit_sw>(branches * 4));
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_S0, 0);

    last_labels_.clear();
    last_labels_.reserve(branches + 1);

    for (size_t j = 0; j < branches; ++j) {
      last_labels_.push_back(sljit_emit_label(c));
      sljit_emit_op1(c, SLJIT_MOV_U32, SLJIT_R2, 0, SLJIT_MEM1(SLJIT_R1), static_cast<sljit_sw>(j * 4));
      sljit_jump* jmp = sljit_emit_cmp(c, SLJIT_NOT_EQUAL | SLJIT_32, SLJIT_R2, 0, SLJIT_IMM, 0);
      sljit_label* after = sljit_emit_label(c);
      sljit_set_label(jmp, after);
      emit_nops(c, spacing - kMinSiteBytes);
    }
    last_labels_.push_back(sljit_emit_label(c));

    // hist_idx wrap: hist_idx = (hist_idx+1 == history_len) ? 0 : hist_idx+1.
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, 1);
    sljit_emit_op2u(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_S1, 0, SLJIT_IMM, static_cast<sljit_sw>(history_len));
    sljit_jump* skip_reset = sljit_emit_jump(c, SLJIT_NOT_ZERO);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 0);
    sljit_label* after_wrap = sljit_emit_label(c);
    sljit_set_label(skip_reset, after_wrap);
  });

  sljit_emit_return_void(c);

  last_branches_ = branches;
  last_spacing_ = spacing;
  branch_history_footprint_internal::g_last_emitted = this;
}

void BranchHistoryFootprint::verify_layout(sljit_compiler* /*c*/) {
  if (last_branches_ == 0 || last_labels_.empty()) {
    return;
  }
  verify_uniform_spacing(last_labels_, last_spacing_, /*strict=*/false, "branch_history_footprint");
}

namespace branch_history_footprint_internal {

LayoutSnapshot last_layout_snapshot() {
  if (g_last_emitted == nullptr) {
    return {};
  }
  return {.labels = g_last_emitted->last_labels_,
          .branches = g_last_emitted->last_branches_,
          .spacing = g_last_emitted->last_spacing_};
}

}  // namespace branch_history_footprint_internal

FERRET_BENCHMARK("branch_history_footprint", BranchHistoryFootprint);

}  // namespace ferret

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

struct BiasConditionalBranchCount;

namespace {

#if defined(__aarch64__) || defined(_M_ARM64)
constexpr size_t kMinSiteBytes = 8;
#elif defined(__x86_64__) || defined(_M_X64)
constexpr size_t kMinSiteBytes = 6;
#else
#error "ferret v1 supports only x86_64 and aarch64"
#endif

// Extra mix constants layered onto mix_seed() so direction assignment
// depends on (seed, branches, nt_branch_pct) only, and outcome fill
// adds (pattern_period, bias_pct). Two independent streams keep the
// per-branch direction set stable across `total_outcomes` sweeps at a
// fixed (branches, nt_branch_pct, seed) — load-bearing for the
// experimental design (spec §7.3).
constexpr uint64_t kNtPctMix   = 0xD6E8FEB86659FD93ULL;
constexpr uint64_t kBiasPctMix = 0x94D049BB133111EBULL;

}  // namespace

namespace bias_conditional_branch_count_internal {

std::vector<uint8_t> assign_directions(size_t branches, int64_t nt_branch_pct, uint64_t seed) {
  std::vector<uint8_t> dirs(branches, 0U);
  if (branches == 0) {
    return dirs;
  }
  size_t nt_count = static_cast<size_t>(static_cast<int64_t>(branches) * nt_branch_pct / 100);
  if (nt_count == 0) {
    return dirs;
  }
  if (nt_count >= branches) {
    std::fill(dirs.begin(), dirs.end(), uint8_t{1});
    return dirs;
  }
  std::vector<size_t> idxs(branches);
  for (size_t i = 0; i < branches; ++i) {
    idxs[i] = i;
  }
  uint64_t mixed = mix_seed(seed, branches, static_cast<uint64_t>(nt_branch_pct)) ^ kNtPctMix;
  std::mt19937_64 rng(mixed);
  for (size_t i = 0; i < nt_count; ++i) {
    std::uniform_int_distribution<size_t> dist(i, branches - 1);
    size_t j = dist(rng);
    std::swap(idxs[i], idxs[j]);
    dirs[idxs[i]] = 1U;
  }
  return dirs;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t pattern_period, int64_t bias_pct,
                                             int64_t nt_branch_pct, uint64_t seed) {
  std::vector<uint32_t> flat(branches * pattern_period, 0U);
  if (branches == 0 || pattern_period == 0) {
    return flat;
  }
  auto dirs = assign_directions(branches, nt_branch_pct, seed);

  uint64_t dir_mix = mix_seed(seed, branches, static_cast<uint64_t>(nt_branch_pct)) ^ kNtPctMix;
  uint64_t fill_mix = mix_seed(dir_mix, pattern_period, static_cast<uint64_t>(bias_pct)) ^ kBiasPctMix;
  std::mt19937_64 rng(fill_mix);

  // Q14 fixed-point Bernoulli draw. r ∈ [0, 0x3fff]; the branch is
  // taken iff r < prob_taken_q14. At bias_pct=100, prob_taken_q14 =
  // 16384 so r < 16384 is always true → always taken. At bias_pct=0,
  // prob_taken_q14 = 0 → never taken.
  auto prob_taken_q14 = [&](bool nt_preferred) -> uint32_t {
    int64_t pct = nt_preferred ? (100 - bias_pct) : bias_pct;
    return static_cast<uint32_t>(pct * 16384 / 100);
  };

  for (size_t t = 0; t < pattern_period; ++t) {
    for (size_t j = 0; j < branches; ++j) {
      uint32_t r = static_cast<uint32_t>(rng() & 0x3fffU);
      flat[t * branches + j] = (r < prob_taken_q14(dirs[j] != 0U)) ? 1U : 0U;
    }
  }
  return flat;
}

struct LayoutSnapshot {
  std::vector<sljit_label*> labels;
  size_t branches;
  size_t spacing;
};

namespace {
const BiasConditionalBranchCount* g_last_emitted = nullptr;
}  // namespace

LayoutSnapshot last_layout_snapshot();

}  // namespace bias_conditional_branch_count_internal

struct BiasConditionalBranchCount : Benchmark {
  std::vector<uint32_t> flat_;
  std::vector<sljit_label*> last_labels_;
  size_t last_branches_ = 0;
  size_t last_spacing_ = 0;

  [[nodiscard]] std::string name() const override { return "bias_conditional_branch_count"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::geom_range("branches", 1, 1 << 13, /*samples_per_octave=*/1),
        Axis::geom_range("total_outcomes", 1 << 13, 1 << 20, /*samples_per_octave=*/1),
    };
  }

  [[nodiscard]] BenchOptions options() const override {
    return {
        BenchOption{.name = "bias_pct", .default_value = 95},
        BenchOption{.name = "nt_branch_pct", .default_value = 50},
        BenchOption{.name = "spacing_bytes", .default_value = 16},
    };
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("branches"); }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return compute_iterations(10'000'000, p.get<size_t>("branches"));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override;
  void verify_layout(sljit_compiler* c) override;
};

void BiasConditionalBranchCount::emit_kernel(sljit_compiler* c, const Params& p) {
  auto branches = p.get<size_t>("branches");
  auto total_outcomes = p.get<size_t>("total_outcomes");
  auto bias_pct = p.get<int64_t>("bias_pct");
  auto nt_branch_pct = p.get<int64_t>("nt_branch_pct");
  auto spacing = p.get<size_t>("spacing_bytes");
  auto seed = static_cast<uint64_t>(p.get<int64_t>("seed"));

  if (branches < 1) {
    throw std::invalid_argument("branches must be >= 1");
  }
  if (total_outcomes < 1) {
    throw std::invalid_argument("total_outcomes must be >= 1");
  }
  if (total_outcomes < branches) {
    throw std::invalid_argument("total_outcomes (" + std::to_string(total_outcomes) +
                                ") must be >= branches (" + std::to_string(branches) +
                                ") so pattern_period >= 1");
  }
  if (bias_pct < 0 || bias_pct > 100) {
    throw std::invalid_argument("bias_pct must be in [0, 100]; got " + std::to_string(bias_pct));
  }
  if (nt_branch_pct < 0 || nt_branch_pct > 100) {
    throw std::invalid_argument("nt_branch_pct must be in [0, 100]; got " + std::to_string(nt_branch_pct));
  }
  if (spacing < kMinSiteBytes) {
    throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                " is smaller than the minimum site encoding (" + std::to_string(kMinSiteBytes) +
                                " bytes) on this architecture");
  }

  size_t pattern_period = total_outcomes / branches;
  size_t iters = iterations(p);

  flat_ = bias_conditional_branch_count_internal::generate_pattern_fill(branches, pattern_period, bias_pct,
                                                                          nt_branch_pct, seed);

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

    // hist_idx wrap.
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, 1);
    sljit_emit_op2u(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_S1, 0, SLJIT_IMM, static_cast<sljit_sw>(pattern_period));
    sljit_jump* skip_reset = sljit_emit_jump(c, SLJIT_NOT_ZERO);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 0);
    sljit_label* after_wrap = sljit_emit_label(c);
    sljit_set_label(skip_reset, after_wrap);
  });

  sljit_emit_return_void(c);

  last_branches_ = branches;
  last_spacing_ = spacing;
  bias_conditional_branch_count_internal::g_last_emitted = this;
}

void BiasConditionalBranchCount::verify_layout(sljit_compiler* /*c*/) {
  if (last_branches_ == 0 || last_labels_.empty()) {
    return;
  }
  verify_uniform_spacing(last_labels_, last_spacing_, /*strict=*/false, "bias_conditional_branch_count");
}

namespace bias_conditional_branch_count_internal {

LayoutSnapshot last_layout_snapshot() {
  if (g_last_emitted == nullptr) {
    return {};
  }
  return {.labels = g_last_emitted->last_labels_,
          .branches = g_last_emitted->last_branches_,
          .spacing = g_last_emitted->last_spacing_};
}

}  // namespace bias_conditional_branch_count_internal

FERRET_BENCHMARK("bias_conditional_branch_count", BiasConditionalBranchCount);

}  // namespace ferret

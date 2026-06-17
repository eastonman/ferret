extern "C" {
#include <sljitLir.h>
}

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "ferret/bench_helpers.hpp"
#include "ferret/benchmark.hpp"
#include "ferret/jit.hpp"
#include "ferret/log.hpp"
#include "ferret/padding.hpp"
#include "ferret/runner.hpp"
#include "ferret/timing.hpp"

namespace ferret {

namespace {
// Minimum bytes a load + cmp-and-branch can occupy (matches
// branch_history_footprint). 8 on AArch64, 6 on x86_64.
#if defined(__aarch64__) || defined(_M_ARM64)
constexpr size_t kMinSiteBytes = 8;
#elif defined(__x86_64__) || defined(_M_X64)
constexpr size_t kMinSiteBytes = 6;
#else
#error "ferret supports only x86_64 and aarch64"
#endif
}  // namespace

struct TrainBetrayLatency;  // forward decl for internal namespace use

namespace train_betray_latency_internal {

enum class FillMode : std::uint8_t { Betray, Control };

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<uint32_t> generate_pattern_fill(size_t branches, size_t train_iters, FillMode mode) {
  const size_t rows = train_iters + 1;
  std::vector<uint32_t> flat(branches * rows, mode == FillMode::Control ? 1U : 0U);
  if (mode == FillMode::Betray) {
    // Training rows are all 1; betrayal row (last) stays 0.
    for (size_t row = 0; row < train_iters; ++row) {
      for (size_t j = 0; j < branches; ++j) {
        flat[(row * branches) + j] = 1U;
      }
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
// Set by emit_kernel as a side effect so tests can inspect the most
// recent post-codegen layout. Anonymous namespace → TU-local.
const TrainBetrayLatency* g_last_emitted = nullptr;
}  // namespace

LayoutSnapshot last_layout_snapshot();  // defined after struct below

}  // namespace train_betray_latency_internal

struct TrainBetrayLatency : Benchmark {
  using FillMode = train_betray_latency_internal::FillMode;

  // Per-emission state. emit_kernel bakes a buffer pointer into the
  // JIT'd code as SLJIT_IMM. We pre-build distinct Betray and Control
  // buffers and emit_kernel picks one based on fill_mode_, so every
  // JittedKernel built during one measure_row call reads from the same
  // (stable, unchanging) buffer — multiple kernels can stay alive
  // simultaneously without invalidating each other's baked pointers.
  std::vector<uint32_t> flat_betray_;
  std::vector<uint32_t> flat_control_;
  std::vector<sljit_label*> last_labels_;
  size_t last_branches_ = 0;
  size_t last_spacing_ = 0;
  FillMode fill_mode_ = FillMode::Betray;

  ~TrainBetrayLatency() override {
    if (train_betray_latency_internal::g_last_emitted == this) {
      train_betray_latency_internal::g_last_emitted = nullptr;
    }
  }

  [[nodiscard]] std::string name() const override { return "train_betray_latency"; }

  // K must be large enough that K * c_misp ≫ timer quantum (24 MHz
  // CNTVCT on Apple Silicon = ~190 cycles/tick) AND ≫ typical OS
  // interrupt cost (~10 µs ≈ 45 K cycles). At K ≥ 16K, the per-trial
  // signal is ~45 µs+, putting interrupts at < 25 % of the measurement
  // — the median across reps stabilizes within a few cycles. Below
  // ~8K, fixed overheads (JIT prologue, training-round cost not
  // captured in differencing) inflate the apparent per-mispredict cost.
  [[nodiscard]] SweepAxes axes() const override {
    return {Axis::geom_range("branches", 16384, 65536, /*samples_per_octave=*/1)};
  }

  [[nodiscard]] BenchOptions options() const override {
    return {
        BenchOption{.name = "train_iters", .default_value = 8},
        BenchOption{.name = "spacing_bytes", .default_value = 16},
    };
  }

  // "Site" for CSV normalization = "mispredict". One betrayal round
  // of K branches per kernel call.
  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("branches"); }

  // One outer-loop cycle = train_iters training rounds + 1 betrayal round.
  // We want exactly one full cycle per kernel call so the betrayal happens
  // on a cold-but-2-trained predictor; running additional cycles would
  // give TAGE the chance to learn the period.
  [[nodiscard]] size_t iterations(const Params& p) const override {
    return static_cast<size_t>(p.get<int64_t>("train_iters") + 1);
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override;
  void verify_layout(sljit_compiler* c) override;
  MeasurementRow measure_row(const Params& p, int reps, int warmup) override;
};

void TrainBetrayLatency::emit_kernel(sljit_compiler* c, const Params& p) {
  auto branches = p.get<size_t>("branches");
  auto train_iters_signed = p.get<int64_t>("train_iters");
  auto spacing = p.get<size_t>("spacing_bytes");

  if (branches < 1) {
    throw std::invalid_argument("branches must be >= 1");
  }
  if (train_iters_signed < 0) {
    throw std::invalid_argument("train_iters must be >= 0; got " + std::to_string(train_iters_signed));
  }
  const auto train_iters = static_cast<size_t>(train_iters_signed);
  if (spacing < kMinSiteBytes) {
    throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                " is smaller than the minimum site encoding (" + std::to_string(kMinSiteBytes) +
                                " bytes) on this architecture");
  }
#if defined(__aarch64__) || defined(_M_ARM64)
  if (spacing % 4 != 0) {
    throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                " must be a multiple of 4 on this architecture");
  }
#endif

  const size_t rows = train_iters + 1;
  const size_t iters = iterations(p);

  // Lazily populate the buffer for the currently-requested fill mode.
  // measure_row pre-fills both once per (K, M); single-kernel callers
  // (verify_layout tests) hit this fall-back path.
  auto& flat = (fill_mode_ == FillMode::Control) ? flat_control_ : flat_betray_;
  if (flat.size() != branches * rows) {
    flat = train_betray_latency_internal::generate_pattern_fill(branches, train_iters, fill_mode_);
  }

  // 3 scratches + 2 saveds, same shape as branch_history_footprint.
  // SLJIT_S0 = flat_base, SLJIT_S1 = hist_idx, SLJIT_R0 = iter counter.
  // SLJIT_R1 = row_ptr, SLJIT_R2 = loaded value.
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/3, /*saved=*/2, /*local_size=*/0);
  sljit_emit_op1(c, SLJIT_MOV, SLJIT_S0, 0, SLJIT_IMM, reinterpret_cast<sljit_sw>(flat.data()));
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

    // hist_idx = (hist_idx + 1 == rows) ? 0 : hist_idx + 1
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_S1, 0, SLJIT_IMM, 1);
    sljit_emit_op2u(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_S1, 0, SLJIT_IMM, static_cast<sljit_sw>(rows));
    sljit_jump* skip_reset = sljit_emit_jump(c, SLJIT_NOT_ZERO);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_S1, 0, SLJIT_IMM, 0);
    sljit_label* after_wrap = sljit_emit_label(c);
    sljit_set_label(skip_reset, after_wrap);
  });

  sljit_emit_return_void(c);

  last_branches_ = branches;
  last_spacing_ = spacing;
  train_betray_latency_internal::g_last_emitted = this;
}

void TrainBetrayLatency::verify_layout(sljit_compiler* /*c*/) {
  if (last_branches_ == 0 || last_labels_.empty()) {
    return;
  }
  verify_uniform_spacing(last_labels_, last_spacing_, /*strict=*/false, "train_betray_latency");
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
MeasurementRow TrainBetrayLatency::measure_row(const Params& p, int reps, int warmup) {
  const auto K = p.get<size_t>("branches");
  const auto M = static_cast<size_t>(p.get<int64_t>("train_iters"));
  if (reps <= 0) {
    throw std::invalid_argument("train_betray_latency: reps must be >= 1");
  }
  // Internal warmup: extra trials whose samples we discard. Smooths
  // out first-kernel cold-cache / freq-ramp jitter without forcing the
  // user to bump --reps. Bounded below at 3 even when the caller
  // requests less; below that the IQM has too few samples to be robust.
  constexpr int kMinInternalWarmup = 3;
  const int internal_warmup = std::max(warmup, kMinInternalWarmup);
  if (internal_warmup > warmup) {
    log::info("train_betray_latency: clamping warmup to {} (was {}) for stable IQM aggregation", internal_warmup,
              warmup);
  }
  const int total_trials = reps + internal_warmup;

  // Pre-build BOTH fill buffers once. They never change across trials.
  // Pre-building decouples emit_kernel's flat-buffer side effect from
  // kernel lifetime, so we can hold many kernels alive simultaneously
  // without the JIT's baked SLJIT_IMM pointers going stale.
  flat_betray_ = train_betray_latency_internal::generate_pattern_fill(K, M, FillMode::Betray);
  flat_control_ = train_betray_latency_internal::generate_pattern_fill(K, M, FillMode::Control);

  // Build ALL kernels first (and keep them alive throughout). This
  // matters because sljit's allocator (sljitExecAllocatorCore.c) reuses
  // freed blocks within the same 64 KiB chunk — emit/free/emit returns
  // the same VA, putting the new "trial" on PCs the predictor just
  // trained at. Holding all kernels alive forces sljit to walk
  // forward through the chunk, giving each kernel a distinct address
  // (typically 64-byte stride between sites).
  std::vector<JittedKernel> kernels_B;
  std::vector<JittedKernel> kernels_C;
  kernels_B.reserve(total_trials);
  kernels_C.reserve(total_trials);
  for (int t = 0; t < total_trials; ++t) {
    fill_mode_ = FillMode::Betray;
    kernels_B.emplace_back(*this, p);
    if (!kernels_B.back().ok()) {
      MeasurementRow fail;
      fail.jit_failed = true;
      fail.iters = 1;
      fail.sites = sites_per_kernel(p);
      fail.reps = static_cast<size_t>(reps);
      return fail;
    }
    fill_mode_ = FillMode::Control;
    kernels_C.emplace_back(*this, p);
    if (!kernels_C.back().ok()) {
      MeasurementRow fail;
      fail.jit_failed = true;
      fail.iters = 1;
      fail.sites = sites_per_kernel(p);
      fail.reps = static_cast<size_t>(reps);
      return fail;
    }
  }

  // Time each pair. Predictor state from prior trials persists, but
  // every trial's B and C kernels are at fresh PCs (different from
  // each other AND from prior trials), so TAGE entries built up by
  // earlier trials don't bias this trial's betrayal. First
  // internal_warmup trials are discarded.
  std::vector<uint64_t> samples;
  samples.reserve(total_trials);
  for (int t = 0; t < total_trials; ++t) {
    auto fn_B = kernels_B[t].fn();
    uint64_t t0 = timing::arch_now_ticks();
    fn_B();
    uint64_t t1 = timing::arch_now_ticks();
    auto fn_C = kernels_C[t].fn();
    uint64_t t2 = timing::arch_now_ticks();
    fn_C();
    uint64_t t3 = timing::arch_now_ticks();
    uint64_t b = t1 - t0;
    uint64_t c = t3 - t2;
    samples.push_back(b > c ? b - c : 0);
  }
  // Discard internal warmup samples.
  samples.erase(samples.begin(), samples.begin() + internal_warmup);
  // Drop trials where the saturating subtract clamped to zero (B < C).
  // These are noise — a single OS interrupt or cache miss biased C
  // larger than B for that trial. Including them in the aggregate
  // pulls the result toward zero. If too many trials clamp, the
  // measurement window is too small for the underlying signal and the
  // user should bump --branches or --reps.
  const size_t before_filter = samples.size();
  std::erase(samples, 0U);
  if (samples.empty()) {
    // Distinct from jit_failed: the JIT worked, but the per-trial
    // signal was below the timer's resolution + scheduling noise.
    // Return a zero-cost row and let the user adjust parameters.
    log::warn(
        "train_betray_latency: all {} timed samples clamped to zero (B<C); "
        "signal below noise floor — increase --branches or --reps",
        before_filter);
    MeasurementRow row;
    row.iters = 1;
    row.sites = sites_per_kernel(p);
    row.reps = 0;  // 0 samples actually contributed
    return row;
  }

  std::ranges::sort(samples);
  MeasurementRow row;
  row.ticks_min = samples.front();
  // Inter-quartile mean: average the middle 50% of samples. Robust
  // against OS-interrupt-inflated outliers (top tail) AND against
  // saturating-subtract zeros from spuriously-fast C runs (bottom
  // tail). With reps=21+ this typically converges within ~0.3 cycles
  // run-to-run.
  if (samples.size() >= 4) {
    const size_t q1 = samples.size() / 4;
    const size_t q3 = samples.size() - (samples.size() / 4);
    uint64_t sum = 0;
    for (size_t i = q1; i < q3; ++i) {
      sum += samples[i];
    }
    row.ticks_median = sum / (q3 - q1);
  } else {
    row.ticks_median = samples[samples.size() / 2];
  }
  // iters=1, sites=K → CSV's ns_per_site == ns per mispredict.
  // reps reports the count of *contributing* samples (post-warmup,
  // post-zero-filter), so downstream tooling can compute correct
  // standard errors.
  row.iters = 1;
  row.sites = sites_per_kernel(p);
  row.reps = samples.size();
  return row;
}

namespace train_betray_latency_internal {

LayoutSnapshot last_layout_snapshot() {
  if (g_last_emitted == nullptr) {
    return {};
  }
  return {.labels = g_last_emitted->last_labels_,
          .branches = g_last_emitted->last_branches_,
          .spacing = g_last_emitted->last_spacing_};
}

}  // namespace train_betray_latency_internal

FERRET_BENCHMARK("train_betray_latency", TrainBetrayLatency);

}  // namespace ferret

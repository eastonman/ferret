extern "C" {
#include <sljitLir.h>
}

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "ferret/benchmark.hpp"

namespace ferret {

namespace {

#if defined(__aarch64__) || defined(_M_ARM64)
constexpr size_t kBranchAlign = 4;
constexpr size_t kMinSiteBytes = 8;    // 4 (ldr) + 4 (cbnz)
// AArch64 ldr (immediate) takes an unsigned 12-bit offset scaled by 4 →
// max byte offset = 4095*4 = 16380, so max branch index = 4095.
constexpr size_t kMaxBranches = 4095;
#elif defined(__x86_64__) || defined(_M_X64)
constexpr size_t kBranchAlign = 1;
constexpr size_t kMinSiteBytes = 9;    // 7 (mov r/m32 with REX+disp32) + 2 (jecxz)
// x86_64 disp32 is signed 32-bit; per-branch displacement is j*4, so the
// theoretical cap is INT32_MAX/4. Practically unreachable for ferret —
// kept as a defensive upper bound rather than an effective constraint.
constexpr size_t kMaxBranches = static_cast<size_t>(INT32_MAX) / 4;
#else
#error "ferret v1 supports only x86_64 and aarch64"
#endif

}  // namespace

struct BranchHistoryFootprint : Benchmark {
  [[nodiscard]] std::string name() const override { return "branch_history_footprint"; }
  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::geom_range("branches", 1, 1 << 9, /*samples_per_octave=*/1),    // 1..512
        Axis::geom_range("history_len", 4, 1 << 12, /*samples_per_octave=*/1),  // 4..4096
    };
  }
  [[nodiscard]] BenchOptions options() const override {
    return {
        BenchOption{.name = "pattern", .default_value = 1},
        BenchOption{.name = "spacing_bytes", .default_value = 16},
    };
  }
  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override {
    return p.get<size_t>("branches");
  }
  [[nodiscard]] size_t iterations(const Params& p) const override {
    return std::max<size_t>(1, 10'000'000 / p.get<size_t>("branches"));
  }
  void emit_kernel(sljit_compiler* /*c*/, const Params& p) override {
    auto branches = p.get<size_t>("branches");
    auto history_len = p.get<size_t>("history_len");
    auto pattern = p.get<int64_t>("pattern");
    auto spacing = p.get<size_t>("spacing_bytes");

    if (branches < 1) {
      throw std::invalid_argument("branches must be >= 1");
    }
    if (history_len < 1) {
      throw std::invalid_argument("history_len must be >= 1");
    }
    if (pattern != 0 && pattern != 1) {
      throw std::invalid_argument("pattern must be 0 (zero) or 1 (random); got " +
                                  std::to_string(pattern));
    }
    if (spacing < kMinSiteBytes) {
      throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                  " is smaller than the site encoding (" +
                                  std::to_string(kMinSiteBytes) + " bytes) on this architecture");
    }
    if (spacing % kBranchAlign != 0) {
      throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                  " must be a multiple of " +
                                  std::to_string(kBranchAlign) + " on this architecture");
    }
    if (branches > kMaxBranches) {
      throw std::invalid_argument("branches=" + std::to_string(branches) +
                                  " exceeds the per-arch limit of " +
                                  std::to_string(kMaxBranches));
    }
  }
};

FERRET_BENCHMARK("branch_history_footprint", BranchHistoryFootprint);

}  // namespace ferret

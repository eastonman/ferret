extern "C" {
#include <sljitLir.h>
}

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "ferret/benchmark.hpp"
#include "ferret/padding.hpp"
#include "ferret/bench_helpers.hpp"
#include "ferret/permute.hpp"

namespace ferret {

namespace {

// Per-arch layout strategy for each direct-branch site.
//   kBranchAlign — required start-address alignment.
//   kJumpBytes   — bytes the emitted jump occupies. Used as
//                  `emit_nops(c, spacing - kJumpBytes)` so each site is
//                  exactly `spacing` bytes wide regardless of N or hop
//                  pattern. Must be the deterministic final size.
//
// AArch64: a single B imm26 (4 bytes) reaches ±128 MB, larger than any
//   realistic ferret kernel. sljit_emit_jump reserves 5 instructions
//   (JUMP_MAX_SIZE; sljitNativeARM_64.c:2528) and reduce_code_size
//   collapses each non-rewritable jump to 1 insn for in-range targets
//   (sljitNativeARM_64.c:443-463). Net: 4 bytes per branch, predictable.
//
// x86_64: sljit_emit_jump can't be made to commit to one encoding
//   without forfeiting 13 B per site (SLJIT_REWRITABLE_JUMP) — reduce
//   picks rel8 (2 B) or rel32 (5 B) per-jump based on the hop distance
//   (sljitNativeX86_common.c:863-867), which is unbounded for
//   sattolo_permute. Instead we hand-emit `JMP rel32` (E9 + 4-byte
//   displacement) via sljit_emit_op_custom and patch the displacement
//   post-generate from sljit_get_label_addr. 5 bytes uniform, any hop.
#if defined(__aarch64__) || defined(_M_ARM64)
constexpr size_t kBranchAlign = 4;
constexpr size_t kJumpBytes = 4;
#elif defined(__x86_64__) || defined(_M_X64)
constexpr size_t kBranchAlign = 1;
constexpr size_t kJumpBytes = 5;
#else
#error "ferret v1 supports only x86_64 and aarch64"
#endif

}  // namespace

// N unconditional direct branches at PC = base + i * spacing_bytes,
// chained so exactly N branches execute per outer-loop iteration; the
// outer loop amortizes the runner's tick-read overhead.
//
// `sattolo_permute=1` rewires the jump targets as a single Hamiltonian
// cycle to defeat spatial I-cache prefetch, isolating the BTB
// contribution. Seed is mixed with branches/spacing so distinct
// (N, spacing) get distinct cycles.
struct DirectBranchFootprint : Benchmark {
  // Captured at emit time, consumed by verify_layout() after
  // sljit_generate_code populates label addresses. last_next_ is only
  // read by the x86_64 displacement-patch path; on AArch64 sljit owns
  // the label resolution.
  std::vector<sljit_label*> last_labels_;
  std::vector<size_t> last_next_;
  size_t last_branches_ = 0;
  size_t last_spacing_ = 0;

  [[nodiscard]] std::string name() const override { return "direct_branch_footprint"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::geom_range("branches", 1, 1 << 15, /*samples_per_octave=*/1),
        Axis::log2_range("spacing_bytes", 16, 128),
    };
  }

  [[nodiscard]] BenchOptions options() const override {
    return {BenchOption{.name = "sattolo_permute", .default_value = 0}};
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("branches"); }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return compute_iterations(10'000'000, p.get<size_t>("branches"));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override {
    auto branches = p.get<size_t>("branches");
    auto spacing = p.get<size_t>("spacing_bytes");
    auto sattolo = p.get<int64_t>("sattolo_permute");
    auto seed = static_cast<uint64_t>(p.get<int64_t>("seed"));
    size_t iters = iterations(p);

    // ISA validation before any sljit state changes — bad params produce
    // no partial compiler state.
    if (spacing < kJumpBytes) {
      throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                  " is smaller than the branch encoding (" + std::to_string(kJumpBytes) +
                                  " bytes) on this architecture");
    }
    if (spacing % kBranchAlign != 0) {
      throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) + " must be a multiple of " +
                                  std::to_string(kBranchAlign) + " on this architecture");
    }

    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 1, 1, 0);

    std::vector<sljit_label*> labels(branches + 1);
    std::vector<sljit_jump*> jumps(branches);

    // Each site: a label marking the jump opcode, then kJumpBytes of
    // branch encoding, then (spacing - kJumpBytes) NOPs of padding.
    //
    // AArch64 path uses sljit_emit_jump; reduce_code_size shrinks it to
    // a single B insn for in-range targets and label addresses are
    // resolved by sljit itself in sljit_generate_code.
    //
    // x86_64 path hand-emits the JMP rel32 opcode (0xE9) followed by a
    // zero placeholder displacement. sljit's reduce pass doesn't touch
    // raw op_custom bytes, so each site is deterministically 5 bytes.
    // verify_layout() patches the displacements once sljit has resolved
    // label addresses.
    emit_outer_loop(c, SLJIT_R0, iters, [&] {
      for (size_t i = 0; i < branches; ++i) {
        labels[i] = sljit_emit_label(c);
#if defined(__aarch64__) || defined(_M_ARM64)
        jumps[i] = sljit_emit_jump(c, SLJIT_JUMP);
#elif defined(__x86_64__) || defined(_M_X64)
        static constexpr std::array<uint8_t, kJumpBytes> jmp_rel32_placeholder = {0xE9, 0, 0, 0, 0};
        sljit_emit_op_custom(c, const_cast<uint8_t*>(jmp_rel32_placeholder.data()), kJumpBytes);
        jumps[i] = nullptr;
#endif
        emit_nops(c, spacing - kJumpBytes);
      }
      labels[branches] = sljit_emit_label(c);
    });

    // next[i] = label index branch i targets; labels[branches] is the
    // post-chain exit so the outer-loop decrement runs once per iteration.
    std::vector<size_t> next(branches);
    if (sattolo == 0) {
      std::iota(next.begin(), next.end(), size_t{1});
    } else {
      uint64_t mixed = mix_seed(seed, branches, spacing);
      next = sattolo_cycle(branches, mixed);
      // Break the unique edge k→0 so the chain exits to labels[branches]
      // after exactly N branches instead of looping forever.
      for (size_t k = 0; k < branches; ++k) {
        if (next[k] == 0) {
          next[k] = branches;
          break;
        }
      }
    }

#if defined(__aarch64__) || defined(_M_ARM64)
    for (size_t i = 0; i < branches; ++i) {
      sljit_set_label(jumps[i], labels[next[i]]);
    }
#endif

    sljit_emit_return_void(c);

    last_labels_ = std::move(labels);
    last_next_ = std::move(next);
    last_branches_ = branches;
    last_spacing_ = spacing;
  }

  // First verifies each branch site sits at base + i*spacing — throws
  // with the per-site delta on the first mismatch. Then on x86_64
  // patches the 4-byte displacement of every hand-emitted JMP rel32.
  // On AArch64 there's nothing to patch; sljit owned label resolution.
  void verify_layout(sljit_compiler* c) override {
    if (last_branches_ == 0 || last_labels_.empty()) {
      return;
    }
    verify_uniform_spacing(last_labels_, last_spacing_, /*strict=*/true, "direct_branch_footprint");

#if defined(__x86_64__) || defined(_M_X64)
    // Write the 4-byte rel32 displacement after each 0xE9 opcode. On
    // x86_64 sljit's default allocator maps the buffer W+X (Linux) or
    // toggles MAP_JIT around writes implicitly (macOS x86_64 — where
    // SLJIT_UPDATE_WX_FLAGS is a no-op anyway), and the writable view
    // sits at exec_addr − executable_offset. The destination kernel
    // is < 2 GB across, so the int32 truncation can't overflow.
    sljit_sw exec_offset = sljit_get_executable_offset(c);
    for (size_t i = 0; i < last_branches_; ++i) {
      sljit_uw jump_addr = sljit_get_label_addr(last_labels_[i]);
      sljit_uw target_addr = sljit_get_label_addr(last_labels_[last_next_[i]]);
      auto disp = static_cast<int32_t>(target_addr - (jump_addr + kJumpBytes));
      // The address came from sljit; this is what set_jump_addr does too —
      // the cast is intrinsic to patching executable memory.
      // NOLINTNEXTLINE(performance-no-int-to-ptr)
      auto* writable = reinterpret_cast<uint8_t*>(jump_addr - static_cast<sljit_uw>(exec_offset) + 1);
      std::memcpy(writable, &disp, sizeof(disp));
    }
#else
    (void)c;
#endif
  }
};

FERRET_BENCHMARK("direct_branch_footprint", DirectBranchFootprint);

}  // namespace ferret

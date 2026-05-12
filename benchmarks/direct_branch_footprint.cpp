extern "C" {
#include <sljitLir.h>
}

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include "ferret/benchmark.hpp"
#include "ferret/padding.hpp"
#include "ferret/permute.hpp"

namespace ferret {

namespace {

// Per-arch layout constraints for a single direct branch site.
//   kBranchAlign  — required start-address alignment for each branch.
//   kMinBranchBytes — smallest possible encoding sljit can emit for an
//                     unconditional direct branch on this ISA. Spacing
//                     smaller than this cannot hold even one branch.
//
// AArch64: every instruction is 4 bytes and must be 4-byte aligned. The
// smallest SLJIT_JUMP encoding for a nearby target is a single B insn
// (4 bytes; sljit reserves 5 instructions but reduce_code_size shrinks
// it back to one for in-range targets).
//
// x86_64: instructions are byte-aligned and variable-length. The
// smallest SLJIT_JUMP sljit emits is a 2-byte EB rel8 short jump
// (used when the target is within ±127 bytes); otherwise it grows to
// 5 bytes (E9 rel32).
#if defined(__aarch64__) || defined(_M_ARM64)
constexpr size_t kBranchAlign = 4;
constexpr size_t kMinBranchBytes = 4;
#elif defined(__x86_64__) || defined(_M_X64)
constexpr size_t kBranchAlign = 1;
constexpr size_t kMinBranchBytes = 2;
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
  [[nodiscard]] std::string name() const override { return "direct_branch_footprint"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::log2_range("branches", 1, 1 << 15),
        Axis::log2_range("spacing_bytes", 16, 128),
    };
  }

  [[nodiscard]] BenchOptions options() const override {
    return {BenchOption{.name = "sattolo_permute", .default_value = 0}};
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("branches"); }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return std::max<size_t>(1, 10'000'000 / p.get<size_t>("branches"));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override {
    auto branches = p.get<size_t>("branches");
    auto spacing = p.get<size_t>("spacing_bytes");
    auto sattolo = p.get<int64_t>("sattolo_permute");
    auto seed = static_cast<uint64_t>(p.get<int64_t>("seed"));
    size_t iters = iterations(p);

    // Static ISA-level checks — done before touching the compiler so a
    // bad parameter point produces no partial state. Alignment: AArch64
    // requires every branch to start on a 4-byte boundary (no-op on
    // x86_64 where kBranchAlign==1). Minimum size: spacing must hold at
    // least one branch instruction.
    if (spacing < kMinBranchBytes) {
      throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) +
                                  " is smaller than the minimum branch encoding (" + std::to_string(kMinBranchBytes) +
                                  " bytes) on this architecture");
    }
    if (spacing % kBranchAlign != 0) {
      throw std::invalid_argument("spacing_bytes=" + std::to_string(spacing) + " must be a multiple of " +
                                  std::to_string(kBranchAlign) + " on this architecture");
    }

    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 1, 1, 0);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, static_cast<sljit_sw>(iters));

    sljit_label* loop_top = sljit_emit_label(c);

    std::vector<sljit_label*> labels(branches + 1);
    std::vector<sljit_jump*> jumps(branches);

    for (size_t i = 0; i < branches; ++i) {
      labels[i] = sljit_emit_label(c);
      size_t before = c->size;
      jumps[i] = sljit_emit_jump(c, SLJIT_JUMP);
      size_t jump_size = c->size - before;
      // sljit's `size` field counts in instructions on aarch64 and bytes
      // on x86_64. emit_nops takes byte counts; on aarch64 we treat each
      // instruction as 4 bytes to keep the math arch-agnostic.
#if defined(__aarch64__) || defined(_M_ARM64)
      jump_size *= 4;
#endif
      if (spacing > jump_size) {
        emit_nops(c, spacing - jump_size);
      }
    }
    labels[branches] = sljit_emit_label(c);

    // next[i] = label index branch i targets; labels[branches] is the
    // post-chain exit so the outer-loop decrement runs once per iteration.
    std::vector<size_t> next(branches);
    if (sattolo == 0) {
      std::iota(next.begin(), next.end(), size_t{1});
    } else {
      uint64_t mixed = seed ^ (static_cast<uint64_t>(branches) * 0x9E3779B97F4A7C15ULL) ^
                       (static_cast<uint64_t>(spacing) * 0xBF58476D1CE4E5B9ULL);
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

    for (size_t i = 0; i < branches; ++i) {
      sljit_set_label(jumps[i], labels[next[i]]);
    }

    sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
    sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
    sljit_set_label(back, loop_top);

    sljit_emit_return_void(c);
  }
};

FERRET_BENCHMARK("direct_branch_footprint", DirectBranchFootprint);

}  // namespace ferret

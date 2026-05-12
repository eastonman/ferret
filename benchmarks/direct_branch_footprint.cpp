extern "C" {
#include <sljitLir.h>
}

#include <algorithm>
#include <vector>

#include "ferret/benchmark.hpp"
#include "ferret/padding.hpp"

namespace ferret {

// N unconditional direct branches, each at PC = base + i * spacing_bytes,
// chained so each branch falls through to the next. Wrapped in an outer
// loop of `iters` so the work amortizes the runner's tick-read overhead.
//
// Padding: each branch is followed by NOPs that bring the next branch's
// start to the requested spacing. We measure the running compiler size
// before/after sljit_emit_jump to know how many bytes the jump consumed,
// then emit `spacing - jump_size` NOPs.
struct DirectBranchFootprint : Benchmark {
  [[nodiscard]] std::string name() const override { return "direct_branch_footprint"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::log2_range("branches", 1, 1 << 15),
        Axis::log2_range("spacing_bytes", 16, 128),
    };
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("branches"); }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return std::max<size_t>(1, 10'000'000 / p.get<size_t>("branches"));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override {
    auto branches = p.get<size_t>("branches");
    auto spacing = p.get<size_t>("spacing_bytes");
    size_t iters = iterations(p);

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

    for (size_t i = 0; i < branches; ++i) {
      sljit_set_label(jumps[i], labels[i + 1]);
    }

    sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
    sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
    sljit_set_label(back, loop_top);

    sljit_emit_return_void(c);
  }
};

FERRET_BENCHMARK("direct_branch_footprint", DirectBranchFootprint);

}  // namespace ferret

extern "C" {
#include <sljitLir.h>
}

#include "ferret/benchmark.hpp"

namespace ferret {

// Dependent ADD chain. Each ADD reads R0 and writes R0, so the chain
// runs at exactly 1 cycle per op on any common core (out-of-order or
// in-order ARM Cortex-A class).
//
// Implementation: emit `full_blocks = chain_length / 1024` outer-loop
// iterations of a 1024-ADD inner block, then a straight-line tail of
// `chain_length % 1024` ADDs. Total ops per fn() == exactly
// `chain_length`. The framework sees iterations=1,
// sites_per_kernel=chain_length, so the per-site metric is ns/op =
// ns/cycle on a 1 IPC core.
//
// For chain_length < UNROLL the loop is skipped entirely and only the
// straight-line tail runs.
struct DependentChainThroughput : Benchmark {
  static constexpr int UNROLL = 1024;

  [[nodiscard]] std::string name() const override { return "dependent_chain_throughput"; }

  [[nodiscard]] SweepAxes axes() const override { return {Axis::values("chain_length", {100'000'000})}; }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("chain_length"); }

  [[nodiscard]] size_t iterations(const Params& /*p*/) const override { return 1; }

  void emit_kernel(sljit_compiler* c, const Params& p) override {
    auto total = p.get<size_t>("chain_length");
    size_t full_blocks = total / UNROLL;
    size_t remainder = total % UNROLL;

    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 2, 2, 0);

    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 1);

    if (full_blocks > 0) {
      sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0, SLJIT_IMM, static_cast<sljit_sw>(full_blocks));

      sljit_label* loop_top = sljit_emit_label(c);

      for (int i = 0; i < UNROLL; ++i) {
        sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
      }

      sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, 1);
      sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
      sljit_set_label(back, loop_top);
    }

    // Straight-line tail: exactly `remainder` ADDs so total ops match
    // chain_length to the op.
    for (size_t i = 0; i < remainder; ++i) {
      sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, 1);
    }

    sljit_emit_return_void(c);
  }
};

FERRET_BENCHMARK("dependent_chain_throughput", DependentChainThroughput);

}  // namespace ferret

extern "C" {
#include <sljitLir.h>
}

#include "ferret/benchmark.hpp"

namespace ferret {

// Dependent ADD chain. Each ADD reads R0 and writes R0, so the chain
// runs at exactly 1 cycle per op on any common core (out-of-order or
// in-order ARM Cortex-A class).
//
// Implementation: emit an inner block of UNROLL dependent ADDs, wrap
// it in an outer loop that runs (chain_length / UNROLL) times. Total
// ops per fn() = chain_length. The framework sees iterations=1,
// sites_per_kernel=chain_length, so the per-site metric is ns/op =
// ns/cycle on a 1 IPC core.
struct DependentChainThroughput : Benchmark {
  static constexpr int UNROLL = 1024;

  std::string name() const override { return "dependent_chain_throughput"; }

  SweepAxes axes() const override {
    return { Axis::values("chain_length", {100'000'000}) };
  }

  size_t sites_per_kernel(const Params& p) const override {
    return p.get<size_t>("chain_length");
  }

  size_t iterations(const Params&) const override { return 1; }

  void emit_kernel(sljit_compiler* c, const Params& p) override {
    size_t total = p.get<size_t>("chain_length");
    size_t outer = total / UNROLL;
    if (outer == 0) outer = 1;

    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 2, 2, 0);

    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 1);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R1, 0,
                   SLJIT_IMM, static_cast<sljit_sw>(outer));

    sljit_label* loop_top = sljit_emit_label(c);

    for (int i = 0; i < UNROLL; ++i) {
      sljit_emit_op2(c, SLJIT_ADD,
                     SLJIT_R0, 0,
                     SLJIT_R0, 0,
                     SLJIT_IMM, 1);
    }

    sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z,
                   SLJIT_R1, 0,
                   SLJIT_R1, 0,
                   SLJIT_IMM, 1);
    sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
    sljit_set_label(back, loop_top);

    sljit_emit_return_void(c);
  }
};

FERRET_BENCHMARK("dependent_chain_throughput", DependentChainThroughput);

}  // namespace ferret

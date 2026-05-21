extern "C" {
#include <sljitLir.h>
}

#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "ferret/bench_helpers.hpp"
#include "ferret/benchmark.hpp"
#include "ferret/permute.hpp"

namespace ferret {

namespace {
// K for variant 2 (path-table dispatch). Defines the binary-tree width.
constexpr int kK = 8;
// call/ret cycles are heavier than single branches, so this benchmark uses
// a smaller op budget than the branch footprint benchmarks.
constexpr size_t kOpBudget = 1'000'000;
}  // namespace

namespace nested_call_depth_internal {

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<uint8_t> generate_path_table(size_t rows, size_t row_stride, uint64_t seed) {
  // The mask `kK - 1` documents that bytes are dispatch indices in [0, kK).
  static_assert(kK == 8, "the 0x7 mask below assumes kK == 8");
  std::vector<uint8_t> out(rows * row_stride);
  std::mt19937_64 rng(seed);
  for (uint8_t& byte : out) {
    byte = static_cast<uint8_t>(rng() & 0x7);
  }
  return out;
}

}  // namespace nested_call_depth_internal

namespace {

// ---------------------------------------------------------------------------
// Variant 0 — single call site per body. BTB-direct predicts every call,
// BTB-indirect predicts every ret (single target per PC). No RAS-forcing.
// Used as a control / baseline to confirm a cliff seen in variants 1/2 is
// really RAS-related rather than I-cache/ITLB/some other artifact.
//
// Derived from ChipsAndCheese/Microbenchmarks ReturnStackTest variant 0:
//   https://github.com/ChipsandCheese/Microbenchmarks/blob/master/AsmGen/tests/ReturnStackTest.cs
// and the C-codegen port at:
//   https://github.com/jiegec/cpu-micro-benchmarks/blob/master/src/ras_size_gen.cpp
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void emit_variant0_single_site(sljit_compiler* c, size_t depth, size_t iters) {
  std::vector<sljit_label*> body_labels(depth + 1, nullptr);
  std::vector<sljit_jump*> pending_calls;
  std::vector<size_t> pending_targets;

  // chain_main: outer loop with one call into BODY_depth.
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/1, /*saved=*/0, /*local_size=*/0);
  emit_outer_loop(c, SLJIT_R0, iters, [&] {
    sljit_jump* call_main = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
    pending_calls.push_back(call_main);
    pending_targets.push_back(depth);
  });
  sljit_emit_return_void(c);

  // BODY_d (d in [1, depth]) — single call to BODY_{d-1}.
  for (size_t d = depth; d >= 1; --d) {
    body_labels[d] = sljit_emit_label(c);
    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/0, /*saved=*/0, /*local_size=*/0);
    sljit_jump* call_body = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
    pending_calls.push_back(call_body);
    pending_targets.push_back(d - 1);
    sljit_emit_return_void(c);
  }

  // BODY_0 — bare ret.
  body_labels[0] = sljit_emit_label(c);
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/0, /*saved=*/0, /*local_size=*/0);
  sljit_emit_return_void(c);

  for (size_t i = 0; i < pending_calls.size(); ++i) {
    sljit_set_label(pending_calls[i], body_labels[pending_targets[i]]);
  }
}

// ---------------------------------------------------------------------------
// Variant 1 — K=2 dispatch driven by bit 0 of the loop counter. Single
// AND+JZ before each CALL, perfectly predictable after one iteration (the
// counter bit alternates 0,1,0,1,…). Each ret has two possible targets per
// PC, defeating last-target indirect predictors. May be partially learnable
// by sufficiently deep TAGE-style indirect predictors.
//
// Counter is threaded through the chain via callee-saved S0; bodies don't
// modify it (declared saved=1 so the C ABI prologue preserves the caller's
// value across the inner CALL).
//
// Derived from ChipsAndCheese/Microbenchmarks ReturnStackTest variant 1:
//   https://github.com/ChipsandCheese/Microbenchmarks/blob/master/AsmGen/tests/ReturnStackTest.cs
// and the C-codegen port at:
//   https://github.com/jiegec/cpu-micro-benchmarks/blob/master/src/ras_size_gen.cpp
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void emit_variant1_counter_bit(sljit_compiler* c, size_t depth, size_t iters) {
  std::vector<sljit_label*> body_labels(depth + 1, nullptr);
  std::vector<sljit_jump*> pending_calls;
  std::vector<size_t> pending_targets;

  // chain_main: counter in S0, single call into BODY_depth per iter.
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/1, /*saved=*/1, /*local_size=*/0);
  emit_outer_loop(c, SLJIT_S0, iters, [&] {
    sljit_jump* call_main = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
    pending_calls.push_back(call_main);
    pending_targets.push_back(depth);
  });
  sljit_emit_return_void(c);

  // BODY_d — test S0 & 1, branch to one of two call sites.
  for (size_t d = depth; d >= 1; --d) {
    body_labels[d] = sljit_emit_label(c);
    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/1, /*saved=*/1, /*local_size=*/0);

    // Test bit 0 of S0 (the iteration counter passed by chain_main).
    sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_S0, 0, SLJIT_IMM, 1);
    sljit_jump* j_to_site_b = sljit_emit_jump(c, SLJIT_ZERO);

    // site_a (bit 0 = 1): call, jump to merge.
    sljit_jump* call_a = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
    pending_calls.push_back(call_a);
    pending_targets.push_back(d - 1);
    sljit_jump* j_to_done = sljit_emit_jump(c, SLJIT_JUMP);

    // site_b (bit 0 = 0): call, fall through to merge.
    sljit_label* lbl_site_b = sljit_emit_label(c);
    sljit_jump* call_b = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
    pending_calls.push_back(call_b);
    pending_targets.push_back(d - 1);

    sljit_label* lbl_done = sljit_emit_label(c);
    sljit_set_label(j_to_site_b, lbl_site_b);
    sljit_set_label(j_to_done, lbl_done);

    sljit_emit_return_void(c);
  }

  // BODY_0 — bare ret. saved=1 so it preserves S0 across its (trivial) frame.
  body_labels[0] = sljit_emit_label(c);
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/0, /*saved=*/1, /*local_size=*/0);
  sljit_emit_return_void(c);

  for (size_t i = 0; i < pending_calls.size(); ++i) {
    sljit_set_label(pending_calls[i], body_labels[pending_targets[i]]);
  }
}

// Emits the K=8 binary-tree dispatch + 8 static call sites + merge tail.
// Used internally by variant 2. `target_d` is the body-label index every
// site calls into. The CALL jumps are appended to pending_calls /
// pending_targets so the caller can wire them up to body_labels once all
// bodies have been emitted.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void emit_k8_dispatch(sljit_compiler* c, sljit_sw table_offset, size_t target_d,
                      std::vector<sljit_jump*>& pending_calls, std::vector<size_t>& pending_targets) {
  // R0 = path_table[row][table_offset], zero-extended from u8.
  sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S1), table_offset);

  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 4);
  sljit_jump* j_to_upper = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 2);
  sljit_jump* j_to_lower_hi = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 1);
  sljit_jump* j_to_site_1 = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  std::vector<sljit_jump*> done_jumps;
  done_jumps.reserve(8);

  auto emit_site = [&](sljit_label*& out_label, bool emit_done_jump) {
    out_label = sljit_emit_label(c);
    sljit_jump* call_jmp = sljit_emit_call(c, SLJIT_CALL, SLJIT_ARGS0V());
    pending_calls.push_back(call_jmp);
    pending_targets.push_back(target_d);
    if (emit_done_jump) {
      done_jumps.push_back(sljit_emit_jump(c, SLJIT_JUMP));
    }
  };

  sljit_label* lbl_site_0 = nullptr;
  sljit_label* lbl_site_1 = nullptr;
  sljit_label* lbl_site_2 = nullptr;
  sljit_label* lbl_site_3 = nullptr;
  sljit_label* lbl_site_4 = nullptr;
  sljit_label* lbl_site_5 = nullptr;
  sljit_label* lbl_site_6 = nullptr;
  sljit_label* lbl_site_7 = nullptr;

  emit_site(lbl_site_0, /*emit_done_jump=*/true);
  emit_site(lbl_site_1, /*emit_done_jump=*/true);

  sljit_label* lbl_lower_hi = sljit_emit_label(c);
  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 1);
  sljit_jump* j_to_site_3 = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  emit_site(lbl_site_2, /*emit_done_jump=*/true);
  emit_site(lbl_site_3, /*emit_done_jump=*/true);

  sljit_label* lbl_upper = sljit_emit_label(c);
  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 2);
  sljit_jump* j_to_upper_hi = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 1);
  sljit_jump* j_to_site_5 = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  emit_site(lbl_site_4, /*emit_done_jump=*/true);
  emit_site(lbl_site_5, /*emit_done_jump=*/true);

  sljit_label* lbl_upper_hi = sljit_emit_label(c);
  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 1);
  sljit_jump* j_to_site_7 = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  emit_site(lbl_site_6, /*emit_done_jump=*/true);
  emit_site(lbl_site_7, /*emit_done_jump=*/false);

  sljit_label* lbl_done = sljit_emit_label(c);

  sljit_set_label(j_to_upper, lbl_upper);
  sljit_set_label(j_to_lower_hi, lbl_lower_hi);
  sljit_set_label(j_to_site_1, lbl_site_1);
  sljit_set_label(j_to_site_3, lbl_site_3);
  sljit_set_label(j_to_upper_hi, lbl_upper_hi);
  sljit_set_label(j_to_site_5, lbl_site_5);
  sljit_set_label(j_to_site_7, lbl_site_7);

  for (sljit_jump* j : done_jumps) {
    sljit_set_label(j, lbl_done);
  }

  (void)lbl_site_0;
  (void)lbl_site_2;
  (void)lbl_site_4;
  (void)lbl_site_6;
}

// ---------------------------------------------------------------------------
// Variant 2 — K=8 dispatch driven by a pre-generated path table. Each ret
// has eight possible target PCs per ret PC; even sophisticated TAGE-style
// indirect predictors cannot memorize all of them across a 256-row table.
// Higher dispatch cost (3 dependent CBs per body) but most robust against
// fallback prediction structures.
// ---------------------------------------------------------------------------
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void emit_variant2_path_table(sljit_compiler* c, size_t depth, size_t iters, size_t rows, const uint8_t* table_ptr) {
  size_t stride = depth + 1;

  std::vector<sljit_label*> body_labels(depth + 1, nullptr);
  std::vector<sljit_jump*> pending_calls;
  std::vector<size_t> pending_targets;

  // chain_main: S0 = counter, S1 = row pointer (recomputed per iter).
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/2, /*saved=*/2, /*local_size=*/0);
  emit_outer_loop(c, SLJIT_S0, iters, [&] {
    // R0 = table_ptr; R1 = (S0 & (rows-1)) * stride; S1 = R0 + R1.
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, reinterpret_cast<sljit_sw>(table_ptr));
    sljit_emit_op2(c, SLJIT_AND, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, static_cast<sljit_sw>(rows - 1));
    sljit_emit_op2(c, SLJIT_MUL, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, static_cast<sljit_sw>(stride));
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_R0, 0, SLJIT_R1, 0);

    // chain_main itself runs the K=8 dispatch at table offset 0.
    emit_k8_dispatch(c, /*table_offset=*/0, /*target_d=*/depth, pending_calls, pending_targets);
  });
  sljit_emit_return_void(c);

  for (size_t d = depth; d >= 1; --d) {
    body_labels[d] = sljit_emit_label(c);
    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/1, /*saved=*/2, /*local_size=*/0);

    emit_k8_dispatch(c, /*table_offset=*/static_cast<sljit_sw>(depth - d + 1),
                     /*target_d=*/d - 1, pending_calls, pending_targets);

    sljit_emit_return_void(c);
  }

  body_labels[0] = sljit_emit_label(c);
  sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/0, /*saved=*/2, /*local_size=*/0);
  sljit_emit_return_void(c);

  for (size_t i = 0; i < pending_calls.size(); ++i) {
    sljit_set_label(pending_calls[i], body_labels[pending_targets[i]]);
  }
}

}  // anonymous namespace

struct NestedCallDepth : Benchmark {
  // Owned per variant=2 sweep point so the JIT'd code can reference an
  // immediate pointer that stays valid for the kernel's lifetime. Never
  // freed until the benchmark instance is destroyed.
  std::vector<std::vector<uint8_t>> path_tables_;

  [[nodiscard]] std::string name() const override { return "nested_call_depth"; }

  [[nodiscard]] SweepAxes axes() const override {
    // variant: 0 = single call site (no RAS-forcing — control / baseline),
    //          1 = K=2 counter-bit dispatch (canonical, near-ideal floor),
    //          2 = K=8 path-table dispatch (most robust BTB-indirect defeat).
    // A swept axis (rather than a scalar option) so users can run all three
    // variants in one invocation — `--variant=0,1,2` — and plot them on a
    // single figure. Default {1} keeps the canonical kernel as the no-flag
    // behavior.
    return {
        Axis::range("depth", 1, 64),
        Axis::values("variant", {1}),
    };
  }

  [[nodiscard]] BenchOptions options() const override {
    // path_table_rows: only used when variant=2. 256 rows × (depth+1) bytes ≤
    // ~16 KB at depth 64, fits L1D on every shipping core. Raise if you
    // suspect deeper indirect-prediction history than 8 bits.
    return {
        BenchOption{.name = "path_table_rows", .default_value = 256},
    };
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("depth") + 1; }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return compute_iterations(kOpBudget, p.get<size_t>("depth") + 1);
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override {
    auto depth = p.get<size_t>("depth");
    auto variant = p.get<int64_t>("variant");

    if (depth < 1) {
      throw std::invalid_argument("nested_call_depth: depth must be >= 1, got " + std::to_string(depth));
    }
    if (variant < 0 || variant > 2) {
      throw std::invalid_argument("nested_call_depth: variant must be 0, 1, or 2; got " + std::to_string(variant));
    }

    auto iters = iterations(p);

    if (variant == 0) {
      emit_variant0_single_site(c, depth, iters);
      return;
    }
    if (variant == 1) {
      emit_variant1_counter_bit(c, depth, iters);
      return;
    }

    // variant == 2 — needs path_table_rows validation and allocation.
    auto path_table_rows = p.get<int64_t>("path_table_rows");
    if (path_table_rows < 2) {
      throw std::invalid_argument("nested_call_depth: path_table_rows must be >= 2 (variant=2), got " +
                                  std::to_string(path_table_rows));
    }
    if ((path_table_rows & (path_table_rows - 1)) != 0) {
      throw std::invalid_argument("nested_call_depth: path_table_rows must be a power of two (variant=2), got " +
                                  std::to_string(path_table_rows));
    }

    auto rows = static_cast<size_t>(path_table_rows);
    auto seed = static_cast<uint64_t>(p.get<int64_t>("seed"));
    uint64_t mixed = mix_seed(seed, static_cast<uint64_t>(depth), 0);
    path_tables_.push_back(nested_call_depth_internal::generate_path_table(rows, depth + 1, mixed));
    emit_variant2_path_table(c, depth, iters, rows, path_tables_.back().data());
  }
};

FERRET_BENCHMARK("nested_call_depth", NestedCallDepth);

}  // namespace ferret

extern "C" {
#include <sljitLir.h>
}

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "ferret/benchmark.hpp"

namespace ferret {

namespace {
constexpr int kK = 8;
}  // namespace

namespace nested_call_depth_internal {

// Seeded xorshift64. Tiny, deterministic, no library dependency.
inline uint64_t xorshift64(uint64_t& state) {
  uint64_t x = state;
  x ^= x << 13;
  x ^= x >> 7;
  x ^= x << 17;
  state = x;
  return x;
}

// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
std::vector<uint8_t> generate_path_table(size_t rows, size_t row_stride, uint64_t seed) {
  // The mask `kK - 1` documents that bytes are dispatch indices in [0, kK).
  static_assert(kK == 8, "the 0x7 mask below assumes kK == 8");
  std::vector<uint8_t> out;
  out.resize(rows * row_stride);
  // Avoid a zero state — xorshift would lock at zero.
  uint64_t s = seed == 0 ? 0x9E3779B97F4A7C15ULL : seed;
  for (uint8_t& byte : out) {
    byte = static_cast<uint8_t>(xorshift64(s) & 0x7);
  }
  return out;
}

}  // namespace nested_call_depth_internal

namespace {

// Emits the K=8 binary-tree dispatch + 8 static call sites + merge tail.
// `target_d` is the body-label index every site calls into. The CALL jumps
// are appended to pending_calls / pending_targets so the caller can wire
// them up to body_labels once all bodies have been emitted.
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters)
void emit_k8_dispatch(sljit_compiler* c, sljit_sw table_offset, size_t target_d,
                      std::vector<sljit_jump*>& pending_calls, std::vector<size_t>& pending_targets) {
  // R0 = path_table[row][table_offset], zero-extended from u8.
  sljit_emit_op1(c, SLJIT_MOV_U8, SLJIT_R0, 0, SLJIT_MEM1(SLJIT_S1), table_offset);

  // ---- Binary-tree dispatch: 3 conditional branches on bits 2, 1, 0. ----
  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 4);
  sljit_jump* j_to_upper = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  // --- lower half (sites 0..3): bit 2 = 0 ---
  sljit_emit_op2u(c, SLJIT_AND | SLJIT_SET_Z, SLJIT_R0, 0, SLJIT_IMM, 2);
  sljit_jump* j_to_lower_hi = sljit_emit_jump(c, SLJIT_NOT_ZERO);

  // --- {0,1}: bit 1 = 0. Bit 0 picks site 0 (=0) vs site 1 (=1). ---
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

}  // anonymous namespace

struct NestedCallDepth : Benchmark {
  // One vector per sweep point; never freed until the benchmark instance
  // is destroyed. Each entry is alive for as long as its corresponding
  // JIT'd kernel is callable, which is guaranteed by the runner: it frees
  // the JIT code before moving to the next param point, but the benchmark
  // instance outlives the whole sweep.
  std::vector<std::vector<uint8_t>> path_tables_;

  [[nodiscard]] std::string name() const override { return "nested_call_depth"; }

  [[nodiscard]] SweepAxes axes() const override { return {Axis::range("depth", 1, 64)}; }

  [[nodiscard]] BenchOptions options() const override {
    return {BenchOption{.name = "path_table_rows", .default_value = 4096}};
  }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return p.get<size_t>("depth") + 1; }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return std::max<size_t>(1, 1'000'000 / (p.get<size_t>("depth") + 1));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override {
    auto depth = p.get<size_t>("depth");
    auto path_table_rows = p.get<int64_t>("path_table_rows");

    if (depth < 1) {
      throw std::invalid_argument("nested_call_depth: depth must be >= 1, got " + std::to_string(depth));
    }
    if (path_table_rows < 2) {
      throw std::invalid_argument("nested_call_depth: path_table_rows must be >= 2, got " +
                                  std::to_string(path_table_rows));
    }
    if ((path_table_rows & (path_table_rows - 1)) != 0) {
      throw std::invalid_argument("nested_call_depth: path_table_rows must be a power of two, got " +
                                  std::to_string(path_table_rows));
    }

    auto iters = iterations(p);

    auto rows = static_cast<size_t>(path_table_rows);
    size_t stride = depth + 1;
    auto seed = static_cast<uint64_t>(p.get<int64_t>("seed"));
    // Mix seed and depth so distinct sweep points use distinct tables.
    uint64_t mixed = seed ^ (static_cast<uint64_t>(depth) * 0x9E3779B97F4A7C15ULL);
    path_tables_.push_back(nested_call_depth_internal::generate_path_table(rows, stride, mixed));
    uint8_t* table_ptr = path_tables_.back().data();

    // body_labels[d] = entry to the body that's `d` levels from the leaf.
    // body_labels[0] = LEAF/BODY_0. body_labels[depth] = BODY_depth (outermost callee).
    // chain_main is its own thing (entered by the runner, not via call from sljit).
    std::vector<sljit_label*> body_labels(depth + 1, nullptr);
    std::vector<sljit_jump*> pending_calls;
    std::vector<size_t> pending_targets;

    // --- chain_main ---
    // S0 = iter counter (callee-saved, survives calls into BODY_depth).
    // S1 = row pointer (callee-saved, set each iteration before dispatch).
    // R0/R1 = scratch (used for row pointer calculation and dispatch byte).
    // Table_ptr is reloaded from an immediate each iteration so it doesn't
    // need a dedicated saved register.
    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/2, /*saved=*/2, /*local_size=*/0);

    // S0 = iters (decremented each loop; callee-saved so it survives body calls)
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_S0, 0, SLJIT_IMM, static_cast<sljit_sw>(iters));

    sljit_label* loop_top = sljit_emit_label(c);

    // Compute row pointer: R0 = table_ptr, R1 = (S0 & (ROWS-1)) * stride
    // S1 = R0 + R1  (i.e. &table_ptr[iter % rows][0])
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, reinterpret_cast<sljit_sw>(table_ptr));
    sljit_emit_op2(c, SLJIT_AND, SLJIT_R1, 0, SLJIT_S0, 0, SLJIT_IMM, static_cast<sljit_sw>(rows - 1));
    sljit_emit_op2(c, SLJIT_MUL, SLJIT_R1, 0, SLJIT_R1, 0, SLJIT_IMM, static_cast<sljit_sw>(stride));
    sljit_emit_op2(c, SLJIT_ADD, SLJIT_S1, 0, SLJIT_R0, 0, SLJIT_R1, 0);

    // chain_main performs K=8 dispatch into BODY_depth, reading path_table[row][0].
    // emit_k8_dispatch uses R0 (clobbered by CALL inside). S0 (iter counter) is safe.
    emit_k8_dispatch(c, /*table_offset=*/0, /*target_d=*/depth, pending_calls, pending_targets);

    sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, SLJIT_S0, 0, SLJIT_S0, 0, SLJIT_IMM, 1);
    sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
    sljit_set_label(back, loop_top);

    sljit_emit_return_void(c);

    // --- BODY_depth ... BODY_1 (each calls the next-inner body) ---
    for (size_t d = depth; d >= 1; --d) {
      body_labels[d] = sljit_emit_label(c);
      // Body reads S1 (row pointer threaded by chain_main). We declare saved=2
      // (same as chain_main) so the prologue/epilogue preserves S0/S1 — though
      // we don't modify them, the calling convention should treat them as
      // preserved across calls.
      sljit_emit_enter(c, 0, SLJIT_ARGS0V(),
                       /*scratches=*/1, /*saved=*/2, /*local_size=*/0);

      emit_k8_dispatch(c,
                       /*table_offset=*/static_cast<sljit_sw>(depth - d + 1),
                       /*target_d=*/d - 1, pending_calls, pending_targets);

      sljit_emit_return_void(c);
    }

    // --- BODY_0 / LEAF ---
    body_labels[0] = sljit_emit_label(c);
    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), /*scratches=*/0, /*saved=*/0, /*local_size=*/0);
    sljit_emit_return_void(c);

    // --- Wire up all pending forward-referenced CALL jumps ---
    for (size_t i = 0; i < pending_calls.size(); ++i) {
      sljit_set_label(pending_calls[i], body_labels[pending_targets[i]]);
    }
  }
};

FERRET_BENCHMARK("nested_call_depth", NestedCallDepth);

}  // namespace ferret

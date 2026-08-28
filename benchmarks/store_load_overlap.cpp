extern "C" {
#include <sljitLir.h>
}

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "ferret/bench_helpers.hpp"
#include "ferret/benchmark.hpp"

namespace ferret {

namespace {

constexpr size_t kOpBudget = 20'000'000;
constexpr size_t kBodyInsns = 4096;

// Mid-cache-line, so a small negative load offset does not cross a
// 128-byte boundary. A line split costs several times the forwarding
// penalty and would swamp the effect under test.
constexpr sljit_sw kSlot = 576;

#if defined(__aarch64__) || defined(_M_ARM64)
constexpr bool kUniformInsnWidth = true;
constexpr size_t kInsnBytes = 4;
#elif defined(__x86_64__) || defined(_M_X64)
constexpr bool kUniformInsnWidth = false;
constexpr size_t kInsnBytes = 0;
#else
#error "ferret supports only x86_64 and aarch64"
#endif

constexpr sljit_s32 kAcc = SLJIT_R0;
constexpr sljit_s32 kTmp = SLJIT_R1;
constexpr sljit_s32 kCounter = SLJIT_R2;
// Points AT the slot, so the store's offset is 0 and the load's is just
// load_offset. Addressing from SLJIT_SP instead would make the load's
// offset 576+delta: unaligned, past the 256-byte unscaled LDUR/STUR
// window, so sljit would prepend an address computation and the link
// would no longer be a fixed instruction count.
constexpr sljit_s32 kBase = SLJIT_R3;

struct KernelParams {
  int64_t load_offset;
  size_t store_width;
  size_t load_width;
  size_t alu_ops;
};

bool is_width(int64_t w) { return w == 4 || w == 8; }

KernelParams validated_params(const Params& p) {
  auto load_offset = p.get<int64_t>("load_offset_bytes");
  auto store_width = p.get<int64_t>("store_width");
  auto load_width = p.get<int64_t>("load_width");
  auto alu_ops = p.get<int64_t>("alu_ops");

  if (!is_width(store_width) || !is_width(load_width)) {
    throw std::invalid_argument("store_width=" + std::to_string(store_width) +
                                " load_width=" + std::to_string(load_width) + ": each must be 4 or 8");
  }
  if (alu_ops < 1) {
    throw std::invalid_argument("alu_ops=" + std::to_string(alu_ops) + " must be >= 1 to carry the chain value");
  }
  // The load must read some of the bytes the store wrote, or there is no
  // dependency at all and the "chain" silently stops being a chain.
  if (load_offset >= store_width || load_offset + load_width <= 0) {
    throw std::invalid_argument("load_offset_bytes=" + std::to_string(load_offset) + " with store_width=" +
                                std::to_string(store_width) + " load_width=" + std::to_string(load_width) +
                                ": the load reads none of the stored bytes, which breaks the dependent chain");
  }
  // Stronger: the load must cover byte 0 of the store. The chain value is
  // carried by adding 1, so only the low byte is guaranteed to change on
  // every link. A load that misses it sees an effectively constant value,
  // the chain goes dead, and the row reports issue throughput rather than
  // forwarding latency — fast, and completely wrong.
  if (load_offset > 0 || load_offset + load_width <= 0) {
    throw std::invalid_argument(
        "load_offset_bytes=" + std::to_string(load_offset) + " with load_width=" + std::to_string(load_width) +
        ": the load must cover byte 0 of the store (require -load_width < offset <= 0), otherwise the "
        "loaded value is constant and the dependent chain dies");
  }
  return {.load_offset = load_offset,
          .store_width = static_cast<size_t>(store_width),
          .load_width = static_cast<size_t>(load_width),
          .alu_ops = static_cast<size_t>(alu_ops)};
}

size_t insns_per_link(const KernelParams& k) { return 2 + k.alu_ops; }
size_t link_repeats(const KernelParams& k) { return compute_iterations(kBodyInsns, insns_per_link(k)); }

}  // namespace

// A serial dependent chain in which the load is deliberately misaligned
// against the store it depends on:
//
//   str  x0,  [sp, #slot]                 store_width bytes
//   ldr  x3,  [sp, #slot + load_offset]   load_width bytes
//   add  x0,  x3, #1
//   <alu_ops-1 further adds>
//
// Per-link cost is the forward latency plus alu_ops. Sweeping the offset
// and the two widths asks what the fast path actually requires: whether
// an overlapping-but-not-identical access qualifies, and whether the two
// sides must be the same width.
//
// A machine that only forwards on a byte-exact, same-width match shows a
// sharp minimum at offset 0 with matched widths and the full fallback
// everywhere else. A machine that forwards any load contained in the
// store shows a flat curve.
//
// Both directions are constrained, and the constraints are not
// cosmetic. `validated_params` rejects any point where the load misses
// byte 0 of the store: the chain is carried by adding 1, so only the low
// byte changes every link, and a load that misses it reads a constant.
// That silently converts the benchmark from a latency measurement into
// an issue-throughput measurement, which looks *fast* and is meaningless.
struct StoreLoadOverlap : Benchmark {
  std::vector<sljit_label*> link_labels_;
  size_t link_bytes_ = 0;

  [[nodiscard]] std::string name() const override { return "store_load_overlap"; }

  // Every point in this rectangle is valid for every width pair, so the
  // default sweep never trips the byte-0 rejection. Wider offsets are
  // reachable from the CLI and will be rejected if they go too far.
  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::values("load_offset_bytes", {-2, -1, 0}),
        Axis::values("store_width", {4, 8}),
        Axis::values("load_width", {4, 8}),
    };
  }

  [[nodiscard]] BenchOptions options() const override { return {BenchOption{.name = "alu_ops", .default_value = 1}}; }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return link_repeats(validated_params(p)); }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return compute_iterations(kOpBudget, sites_per_kernel(p) * insns_per_link(validated_params(p)));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override {
    auto k = validated_params(p);
    size_t repeats = link_repeats(k);
    size_t iters = iterations(p);

    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 4, 0, static_cast<sljit_s32>(kSlot) + 1024);
    sljit_emit_op1(c, SLJIT_MOV, kAcc, 0, SLJIT_IMM, 1);
    sljit_get_local_base(c, kBase, 0, kSlot);

    const sljit_s32 st_op = k.store_width == 4 ? SLJIT_MOV_U32 : SLJIT_MOV;
    const sljit_s32 ld_op = k.load_width == 4 ? SLJIT_MOV_U32 : SLJIT_MOV;
    const auto ld_off = static_cast<sljit_sw>(k.load_offset);

    link_labels_.clear();
    link_bytes_ = (2 + k.alu_ops) * kInsnBytes;

    emit_outer_loop(c, kCounter, iters, [&] {
      for (size_t r = 0; r < repeats; ++r) {
        if constexpr (kUniformInsnWidth) {
          link_labels_.push_back(sljit_emit_label(c));
        }
        sljit_emit_op1(c, st_op, SLJIT_MEM1(kBase), 0, kAcc, 0);
        sljit_emit_op1(c, ld_op, kTmp, 0, SLJIT_MEM1(kBase), ld_off);
        sljit_emit_op2(c, SLJIT_ADD, kAcc, 0, kTmp, 0, SLJIT_IMM, kChainCarryImm);
        for (size_t a = 1; a < k.alu_ops; ++a) {
          sljit_emit_op2(c, SLJIT_ADD, kAcc, 0, kAcc, 0, SLJIT_IMM, kChainCarryImm);
        }
      }
    });

    sljit_emit_return_void(c);
  }

  // An unaligned offset can push sljit off the scaled-immediate encoding
  // and into a two-instruction address computation. That extra op is off
  // the critical path, so it would not change the latency much, but it
  // would break the "one link is exactly N instructions" accounting the
  // per-site normalization relies on. Fail the row instead.
  void verify_layout(sljit_compiler* c) override {
    (void)c;
    if constexpr (kUniformInsnWidth) {
      verify_uniform_spacing(link_labels_, link_bytes_, /*strict=*/true, "store_load_overlap");
    }
  }
};

FERRET_BENCHMARK("store_load_overlap", StoreLoadOverlap);

}  // namespace ferret

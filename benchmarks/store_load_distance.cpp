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

// The emitted loop body is held to a roughly constant *instruction*
// count rather than a constant link count, so sweeping `separation` or
// `alu_ops` does not also sweep the L1I footprint. Getting this wrong
// makes instruction-cache pressure masquerade as a distance effect.
constexpr size_t kBodyInsns = 4096;

// Chain slot, placed mid-cache-line so nothing here straddles a line.
constexpr sljit_sw kSlot = 576;
// Filler stores and loads live well clear of the chain slot, one per
// 128-byte line, so they cannot contend with the line under test.
constexpr sljit_sw kFillerMem = 8192;
constexpr sljit_sw kFillerStride = 128;

// See kMaxLocalOffset in store_load_footprint.cpp: an 8-byte-aligned
// AArch64 LDR/STR immediate reaches 32760, less the 16-byte
// SLJIT_LOCALS_OFFSET_BASE. Filler memory ops must stay inside it too.
constexpr sljit_sw kMaxLocalOffset = 32744;

#if defined(__aarch64__) || defined(_M_ARM64)
constexpr bool kUniformInsnWidth = true;
constexpr size_t kInsnBytes = 4;
#elif defined(__x86_64__) || defined(_M_X64)
constexpr bool kUniformInsnWidth = false;
constexpr size_t kInsnBytes = 0;
#else
#error "ferret supports only x86_64 and aarch64"
#endif

// What sits between the store and the load. Every kind occupies exactly
// one instruction, so `separation` is both an instruction count and the
// emitted byte stride divided by kInsnBytes.
enum class Filler : uint8_t {
  Alu = 0,       // independent ADD, rotating destinations
  Nop = 1,       // occupies a slot and nothing else
  Store = 2,     // independent store, distinct address per position
  Load = 3,      // independent load from a never-stored region
  Branch = 4,    // taken branch to the next instruction: ends a fetch block
  BaseWrite = 5  // rewrites the address base register with its own value
};

constexpr int64_t kFillerMin = 0;
constexpr int64_t kFillerMax = 5;

// Rotating destinations for the filler ops. A single shared destination
// makes loads look artificially cheap and skews the whole curve.
constexpr sljit_s32 kAcc = SLJIT_R0;
constexpr sljit_s32 kTmp = SLJIT_R1;
constexpr sljit_s32 kCounter = SLJIT_R2;
constexpr sljit_s32 kConst = SLJIT_R3;
constexpr sljit_s32 kBase = SLJIT_R4;
constexpr sljit_s32 kFillFirst = SLJIT_R5;
constexpr size_t kFillRegs = 8;
constexpr sljit_s32 kScratches = 5 + static_cast<sljit_s32>(kFillRegs);

struct KernelParams {
  size_t separation;
  size_t alu_ops;
  Filler filler;
};

// A BaseWrite filler has to have a base register it may legally write,
// so those rows address the chain slot through a general register.
bool needs_gpr_base(Filler f) { return f == Filler::BaseWrite; }

KernelParams validated_params(const Params& p) {
  auto separation = p.get<int64_t>("separation");
  auto filler = p.get<int64_t>("filler");
  auto alu_ops = p.get<int64_t>("alu_ops");

  if (separation < 0) {
    throw std::invalid_argument("separation=" + std::to_string(separation) + " must be >= 0");
  }
  if (filler < kFillerMin || filler > kFillerMax) {
    throw std::invalid_argument("filler=" + std::to_string(filler) +
                                " must be 0 (alu), 1 (nop), 2 (store), 3 (load), 4 (branch) or 5 (base_write)");
  }
  if (alu_ops < 1) {
    throw std::invalid_argument("alu_ops=" + std::to_string(alu_ops) +
                                ": the chain needs at least one ALU op to carry the loaded value back to the "
                                "store's source register without the two registers coinciding");
  }
  sljit_sw max_filler_off = kFillerMem + (static_cast<sljit_sw>(separation) * kFillerStride);
  if (max_filler_off > kMaxLocalOffset) {
    throw std::invalid_argument("separation=" + std::to_string(separation) + " needs a filler offset of " +
                                std::to_string(max_filler_off) + ", past the " + std::to_string(kMaxLocalOffset) +
                                "-byte single-instruction addressing window; lower separation");
  }
  return {.separation = static_cast<size_t>(separation),
          .alu_ops = static_cast<size_t>(alu_ops),
          .filler = static_cast<Filler>(filler)};
}

size_t insns_per_link(const KernelParams& k) { return 1 + k.separation + 1 + k.alu_ops; }

size_t link_repeats(const KernelParams& k) { return compute_iterations(kBodyInsns, insns_per_link(k)); }

}  // namespace

// A serial dependent chain whose store and load are pushed `separation`
// instructions apart:
//
//   str  x0, [base, #slot]
//   <separation filler instructions>
//   ldr  x3, [base, #slot]
//   add  x0, x3, #1
//   <alu_ops-1 further adds>
//
// The chain is store-data -> load-result -> ALU -> next store-data, so
// the per-link cost is the forward latency plus `alu_ops`. Subtracting
// alu_ops leaves the forward cost on its own.
//
// A core that resolves the load when the two are adjacent, but not when
// they are a few instructions apart, produces a curve that is fast at
// separation 0, worst at 1, and recovers as separation grows. Where it
// returns to zero measures the width of whatever window the mechanism
// needs the pair to fall outside of.
//
// `filler` asks whether the *kind* of intervening instruction matters or
// only the count. Instructions that end a fetch block (taken branches)
// can behave differently from ones that merely occupy a slot, and
// `base_write` tests whether rewriting the address base register — with
// its own value, so the address is unchanged — invalidates the match.
//
// This benchmark deliberately does not vary how many store/load pairs
// are in flight at once; the chain is serial, so exactly one is. Probing
// the capacity of the tracking structure needs a different kernel.
struct StoreLoadDistance : Benchmark {
  std::vector<sljit_label*> link_labels_;
  size_t link_bytes_ = 0;

  [[nodiscard]] std::string name() const override { return "store_load_distance"; }

  // `separation` is linear, not geometric: the interesting structure is
  // a handful of instructions wide, so every integer step matters and a
  // log2 sweep would step straight over it.
  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::range("separation", 0, 16),
        Axis::values("filler", {0, 1, 2, 3, 4, 5}),
    };
  }

  // alu_ops buys a latency shadow for the filler work to hide in, so the
  // measurement stays latency-bound rather than issue-bound. It is also
  // the calibration knob: cost must move by exactly one cycle per added
  // op, and a slope that is not 1.0 means the chain is not serial.
  [[nodiscard]] BenchOptions options() const override { return {BenchOption{.name = "alu_ops", .default_value = 16}}; }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override { return link_repeats(validated_params(p)); }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return compute_iterations(kOpBudget, sites_per_kernel(p) * insns_per_link(validated_params(p)));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override {
    auto k = validated_params(p);
    size_t repeats = link_repeats(k);
    size_t iters = iterations(p);
    auto local_size = static_cast<sljit_s32>(kFillerMem + (static_cast<sljit_sw>(k.separation + 1) * kFillerStride));

    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), kScratches, 0, local_size);
    sljit_emit_op1(c, SLJIT_MOV, kAcc, 0, SLJIT_IMM, 1);
    sljit_emit_op1(c, SLJIT_MOV, kConst, 0, SLJIT_IMM, 7);
    // SLJIT_SP is only legal as MEM1(SLJIT_SP), so a filler that writes
    // the base needs an addressable copy of it.
    sljit_get_local_base(c, kBase, 0, 0);
    const sljit_s32 base = needs_gpr_base(k.filler) ? kBase : SLJIT_SP;

    link_labels_.clear();
    link_bytes_ = (2 + k.separation + k.alu_ops) * kInsnBytes;

    emit_outer_loop(c, kCounter, iters, [&] {
      for (size_t r = 0; r < repeats; ++r) {
        if constexpr (kUniformInsnWidth) {
          link_labels_.push_back(sljit_emit_label(c));
        }
        sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(base), kSlot, kAcc, 0);
        for (size_t i = 0; i < k.separation; ++i) {
          emit_filler(c, k.filler, i);
        }
        sljit_emit_op1(c, SLJIT_MOV, kTmp, 0, SLJIT_MEM1(base), kSlot);
        sljit_emit_op2(c, SLJIT_ADD, kAcc, 0, kTmp, 0, SLJIT_IMM, kChainCarryImm);
        for (size_t a = 1; a < k.alu_ops; ++a) {
          sljit_emit_op2(c, SLJIT_ADD, kAcc, 0, kAcc, 0, SLJIT_IMM, kChainCarryImm);
        }
      }
    });

    sljit_emit_return_void(c);
  }

  // Every filler kind is one instruction, so a link is exactly
  // (2 + separation + alu_ops) instructions. sljit silently re-bases with
  // an extra ADDI once an offset leaves the single-instruction window,
  // and a taken branch could in principle be encoded differently; this
  // turns either into a failed row rather than a quietly inflated one.
  void verify_layout(sljit_compiler* c) override {
    (void)c;
    if constexpr (kUniformInsnWidth) {
      verify_uniform_spacing(link_labels_, link_bytes_, /*strict=*/true, "store_load_distance");
    }
  }

 private:
  static void emit_filler(sljit_compiler* c, Filler f, size_t i) {
    auto dst = static_cast<sljit_s32>(kFillFirst + (i % kFillRegs));
    auto off = static_cast<sljit_sw>(kFillerMem + (static_cast<sljit_sw>(i) * kFillerStride));
    switch (f) {
      case Filler::Alu:
        sljit_emit_op2(c, SLJIT_ADD, dst, 0, kConst, 0, SLJIT_IMM, 1);
        break;
      case Filler::Nop:
        sljit_emit_op0(c, SLJIT_NOP);
        break;
      case Filler::Store:
        sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(SLJIT_SP), off, kConst, 0);
        break;
      case Filler::Load:
        sljit_emit_op1(c, SLJIT_MOV, dst, 0, SLJIT_MEM1(SLJIT_SP), off);
        break;
      case Filler::Branch: {
        sljit_jump* j = sljit_emit_jump(c, SLJIT_JUMP);
        sljit_set_label(j, sljit_emit_label(c));
        break;
      }
      case Filler::BaseWrite:
        // Adding zero leaves the address untouched but still writes the
        // architectural register the store and load are addressing through.
        sljit_emit_op2(c, SLJIT_ADD, kBase, 0, kBase, 0, SLJIT_IMM, 0);
        break;
    }
  }
};

FERRET_BENCHMARK("store_load_distance", StoreLoadDistance);

}  // namespace ferret

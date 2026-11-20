extern "C" {
#include <sljitLir.h>
}

#include <algorithm>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

#include "ferret/bench_helpers.hpp"
#include "ferret/benchmark.hpp"

namespace ferret {

namespace {

constexpr size_t kOpBudget = 20'000'000;

// Links emitted per outer-loop iteration, held constant across the
// `addresses` axis by repeating the address rotation. Without this,
// addresses=1 would be three instructions inside a loop and the loop
// overhead would swamp the per-link measurement.
constexpr size_t kUnrollLinks = 512;

// Ceiling on the emitted loop body, in instructions. kUnrollLinks alone
// fixes the *link* count, so the body would grow without bound as
// alu_ops rises -- at alu_ops=64 that is 33 792 instructions, 132 KB,
// closing on the 192 KB L1I of an M4 P-core. Capping the repeat count
// keeps instruction-cache pressure out of the measurement. The cap only
// binds for alu_ops above ~5, so the default sweep is unaffected.
constexpr size_t kMaxBodyInsns = 8192;

// Every store and load moves one machine word.
constexpr size_t kWordBytes = 8;

// AArch64 encodes an 8-byte-aligned LDR/STR immediate offset in 12 bits
// scaled by 8, so a single instruction reaches offset <= 32760 (see
// emit_op_mem in sljit's sljitNativeARM_64.c). Past that sljit silently
// prepends an ADDI to re-base, which inflates the per-link instruction
// count and therefore the measured latency. SLJIT_SP-relative offsets
// additionally carry SLJIT_LOCALS_OFFSET_BASE (16 bytes on AArch64), so
// the usable window for the offsets we choose is 32744.
//
// Applied on x86_64 too, where disp32 imposes no comparable limit. Both
// architectures then sweep an identical parameter space and their CSVs
// stay directly comparable.
constexpr size_t kMaxLocalOffset = 32744;

// Whether every instruction in a link occupies the same number of bytes.
// True on AArch64 (fixed 4-byte encoding), which lets verify_layout
// assert an exact per-link byte stride and so catch any extra
// instruction sljit slipped in. False on x86_64, where LDR/STR
// displacement width varies with the offset (disp8 vs disp32) and a
// uniform stride does not hold by construction.
#if defined(__aarch64__) || defined(_M_ARM64)
constexpr bool kUniformInsnWidth = true;
constexpr size_t kInsnBytes = 4;
#elif defined(__x86_64__) || defined(_M_X64)
constexpr bool kUniformInsnWidth = false;
constexpr size_t kInsnBytes = 0;
#else
#error "ferret supports only x86_64 and aarch64"
#endif

// Instructions in one link: the store, the load, and the carry ops.
size_t insns_per_link(size_t alu_ops) { return 2 + alu_ops; }

// Address rotation repeats: enough to fill kUnrollLinks links, but never
// so many that the loop body outgrows kMaxBodyInsns.
size_t link_repeats(size_t addresses, size_t alu_ops) {
  size_t by_links = compute_iterations(kUnrollLinks, addresses);
  size_t by_insns = compute_iterations(kMaxBodyInsns, addresses * insns_per_link(alu_ops));
  return std::max<size_t>(1, std::min(by_links, by_insns));
}

// One validated parameter point.
struct KernelParams {
  size_t addresses;
  size_t stride;
  size_t alu_ops;
  int64_t base_sel;
};

// Reads and validates a parameter point. Kept out of emit_kernel so
// every rejection happens before the caller touches the compiler and no
// partial sljit state can survive a bad point.
KernelParams validated_params(const Params& p) {
  auto addresses = p.get<size_t>("addresses");
  auto stride = p.get<size_t>("stride_bytes");
  // Read as signed so a negative override is rejected here with a
  // benchmark-specific message rather than by Params::get<size_t>.
  auto alu_ops = p.get<int64_t>("alu_ops");
  auto base_sel = p.get<int64_t>("base_reg");

  if (addresses == 0) {
    throw std::invalid_argument("addresses=0: the chain needs at least one address");
  }
  if (stride < kWordBytes || stride % kWordBytes != 0) {
    throw std::invalid_argument("stride_bytes=" + std::to_string(stride) + " must be a positive multiple of " +
                                std::to_string(kWordBytes));
  }
  if (base_sel < 0 || base_sel > 3) {
    throw std::invalid_argument("base_reg=" + std::to_string(base_sel) +
                                " must be 0 (sp/sp), 1 (gpr/gpr), 2 (sp store, gpr load) or 3 (gpr store, sp load)");
  }
  // With no ALU op there is nothing to carry the loaded value back into
  // the store's source register, so the link would have to load straight
  // into that register. That is a structurally different kernel — the
  // stored value then never changes, and the store's data register and
  // the load's destination register coincide — so it cannot serve as the
  // alu_ops=0 point of this benchmark's line. Measured on an M4 Pro that
  // variant runs ~4.8 cycles per link against ~2.0 for alu_ops=1, which
  // is exactly the confusion this rejection exists to prevent.
  if (alu_ops < 1) {
    throw std::invalid_argument("alu_ops=" + std::to_string(alu_ops) +
                                ": the chain needs at least one ALU op to carry the loaded value back to the "
                                "store's source register without the two registers coinciding");
  }
  size_t max_offset = (addresses - 1) * stride;
  if (max_offset > kMaxLocalOffset) {
    throw std::invalid_argument("addresses=" + std::to_string(addresses) + " x stride_bytes=" + std::to_string(stride) +
                                " needs offset " + std::to_string(max_offset) + ", past the " +
                                std::to_string(kMaxLocalOffset) +
                                "-byte single-instruction addressing window; lower either axis");
  }
  return {.addresses = addresses, .stride = stride, .alu_ops = static_cast<size_t>(alu_ops), .base_sel = base_sel};
}

}  // namespace

// A strictly serial dependent chain threaded through memory, rotating
// over `addresses` distinct stack slots:
//
//   str  x0, [base, #i*stride]     ; store the chain value
//   ldr  x3, [base, #i*stride]     ; read it back
//   add  x0, x3, #1                ; carry it to the next link
//
// Every link is store-data -> load-result -> ALU -> next store-data, so
// the per-link cost is the machine's store-to-load forward latency plus
// `alu_ops`. A core that resolves the load at rename time (memory
// renaming) pays only the ALU op; a core that forwards through the
// store queue pays its full forwarding latency.
//
// Sweeping `addresses` separates "the mechanism only recognizes a
// repeat of the immediately preceding store" from "it handles an
// arbitrary rotating set of slots", and `stride_bytes` separates
// per-address handling from line- or page-granular handling. The whole
// default sweep stays L1D-resident by construction (max footprint
// 32 KB, see kMaxLocalOffset), so no feature of the curve can be a
// cache-capacity effect.
//
// Neither axis probes the *capacity* of whatever tracks store/load
// pairs. The chain is serial, so exactly one pair is ever in flight and
// there is nothing to exhaust; rotating the address changes which slot
// is used, not how many pairs are tracked at once. A capacity probe
// needs concurrently live pairs — independent interleaved chains, one
// register per chain — which is a different kernel.
//
// `base_reg` selects which register name the store and the load each use
// to reach the one address:
//
//   0  str [sp,#i]  / ldr [sp,#i]
//   1  str [xN,#i]  / ldr [xN,#i]    xN copied from SP via sljit_get_local_base
//   2  str [sp,#i]  / ldr [xN,#i]    same bytes, different register NAME
//   3  str [xN,#i]  / ldr [sp,#i]
//
// All four address identical bytes with identical offsets and an
// identical instruction count. Forms 0 and 1 say whether the stack
// pointer is special. Forms 2 and 3 are the sharper test: xN provably
// holds exactly SP, so if they behave differently from 0 and 1 the
// hardware is matching on the register NAME rather than on the computed
// address — i.e. the comparison happens before any address exists.
struct StoreLoadFootprint : Benchmark {
  // Captured at emit time, consumed by verify_layout() once
  // sljit_generate_code has populated label addresses. Only populated
  // when kUniformInsnWidth.
  std::vector<sljit_label*> link_labels_;
  size_t link_bytes_ = 0;

  [[nodiscard]] std::string name() const override { return "store_load_footprint"; }

  [[nodiscard]] SweepAxes axes() const override {
    return {
        Axis::geom_range("addresses", 1, 1 << 8, /*samples_per_octave=*/1),
        Axis::log2_range("stride_bytes", 8, 128),
        Axis::values("base_reg", {0, 1, 2, 3}),
    };
  }

  // alu_ops is a calibration knob: each additional ALU op must lengthen
  // the chain by exactly one cycle. Fitting cycles-per-link against
  // alu_ops recovers the forward latency as the intercept, and a slope
  // that is not 1.0 means the chain is not serial and the rest of the
  // row means nothing. One op is the floor — see the rejection in
  // validated_params.
  [[nodiscard]] BenchOptions options() const override { return {BenchOption{.name = "alu_ops", .default_value = 1}}; }

  [[nodiscard]] size_t sites_per_kernel(const Params& p) const override {
    auto k = validated_params(p);
    return k.addresses * link_repeats(k.addresses, k.alu_ops);
  }

  [[nodiscard]] size_t iterations(const Params& p) const override {
    return compute_iterations(kOpBudget, sites_per_kernel(p));
  }

  void emit_kernel(sljit_compiler* c, const Params& p) override {
    // Validate before any sljit state changes — a bad point must leave
    // no partial compiler state behind.
    auto [addresses, stride, alu_ops, base_sel] = validated_params(p);

    size_t repeats = link_repeats(addresses, alu_ops);
    size_t iters = iterations(p);
    auto local_size = static_cast<sljit_s32>(addresses * stride);

    // R0 chain value, R1 outer-loop counter, R2 general base register,
    // R3 load destination.
    sljit_emit_enter(c, 0, SLJIT_ARGS0V(), 4, 0, local_size);
    sljit_emit_op1(c, SLJIT_MOV, SLJIT_R0, 0, SLJIT_IMM, 1);

    // R2 holds exactly what SLJIT_SP holds. Hoisted out of the loop so
    // every base_reg form emits an instruction-for-instruction identical
    // link and only the register *name* in the encoding differs.
    const bool store_gpr = base_sel == 1 || base_sel == 3;
    const bool load_gpr = base_sel == 1 || base_sel == 2;
    if (store_gpr || load_gpr) {
      sljit_get_local_base(c, SLJIT_R2, 0, 0);
    }
    const sljit_s32 st_base = store_gpr ? SLJIT_R2 : SLJIT_SP;
    const sljit_s32 ld_base = load_gpr ? SLJIT_R2 : SLJIT_SP;

    link_labels_.clear();
    link_bytes_ = (2 + alu_ops) * kInsnBytes;

    emit_outer_loop(c, SLJIT_R1, iters, [&] {
      for (size_t r = 0; r < repeats; ++r) {
        for (size_t i = 0; i < addresses; ++i) {
          auto off = static_cast<sljit_sw>(i * stride);
          // Only the first rotation is labelled: later rotations reuse
          // the same offsets, so they cannot encode differently.
          if constexpr (kUniformInsnWidth) {
            if (r == 0) {
              link_labels_.push_back(sljit_emit_label(c));
            }
          }
          sljit_emit_op1(c, SLJIT_MOV, SLJIT_MEM1(st_base), off, SLJIT_R0, 0);
          // Load into R3 rather than R0 so the store's data register and
          // the load's destination register differ. Sharing one register
          // would leave the chain value unchanged for the whole run and
          // make a same-register peephole indistinguishable from a
          // genuinely fast forward.
          sljit_emit_op1(c, SLJIT_MOV, SLJIT_R3, 0, SLJIT_MEM1(ld_base), off);
          sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R3, 0, SLJIT_IMM, kChainCarryImm);
          for (size_t a = 1; a < alu_ops; ++a) {
            sljit_emit_op2(c, SLJIT_ADD, SLJIT_R0, 0, SLJIT_R0, 0, SLJIT_IMM, kChainCarryImm);
          }
        }
      }
    });

    sljit_emit_return_void(c);
  }

  // Asserts every link occupies exactly link_bytes_. sljit re-bases with
  // an extra ADDI once an offset leaves the single-instruction window,
  // which would inflate per-link latency by a whole instruction without
  // any other visible symptom; this turns that into a failed row.
  void verify_layout(sljit_compiler* c) override {
    (void)c;
    if constexpr (kUniformInsnWidth) {
      verify_uniform_spacing(link_labels_, link_bytes_, /*strict=*/true, "store_load_footprint");
    }
  }
};

FERRET_BENCHMARK("store_load_footprint", StoreLoadFootprint);

}  // namespace ferret

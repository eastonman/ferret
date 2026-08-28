#include <gtest/gtest.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

extern "C" {
#include <sljitLir.h>
}

#include "ferret/benchmark.hpp"
#include "ferret/jit.hpp"
#include "sljit_test_helpers.hpp"

using ferret::testing::CompilerHandle;
using ferret::testing::find_option;

namespace {

ferret::Params make_params(int64_t addresses, int64_t stride_bytes, int64_t base_reg = 0, int64_t alu_ops = 1) {
  ferret::Params p;
  p.set("addresses", addresses);
  p.set("stride_bytes", stride_bytes);
  p.set("base_reg", base_reg);
  p.set("alu_ops", alu_ops);
  p.set("seed", 1);
  return p;
}

// Links emitted per outer-loop iteration; mirrors kUnrollLinks in
// benchmarks/store_load_footprint.cpp.
constexpr size_t kUnrollLinks = 512;

}  // namespace

// ---- Registry / shape ----

TEST(StoreLoadFootprint, RegistryLookupReturnsBenchmark) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->name(), "store_load_footprint");
}

TEST(StoreLoadFootprint, ExposesThreeAxesInCsvColumnOrder) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  auto axes = b->axes();
  ASSERT_EQ(axes.size(), 3u);
  EXPECT_EQ(axes[0].name(), "addresses");
  EXPECT_EQ(axes[1].name(), "stride_bytes");
  EXPECT_EQ(axes[2].name(), "base_reg");
}

TEST(StoreLoadFootprint, AddressesAxisExpansionMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  const std::vector<int64_t> expected{1, 2, 4, 8, 16, 32, 64, 128, 256};
  EXPECT_EQ(b->axes()[0].expand(), expected);
}

TEST(StoreLoadFootprint, StrideBytesAxisExpansionMatchesSpec) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  const std::vector<int64_t> expected{8, 16, 32, 64, 128};
  EXPECT_EQ(b->axes()[1].expand(), expected);
}

// Every default (addresses, stride_bytes) pair must stay inside the
// single-instruction addressing window; the widest is 255 * 128.
TEST(StoreLoadFootprint, DefaultAxisProductStaysInsideAddressingWindow) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  auto addresses = b->axes()[0].expand();
  auto strides = b->axes()[1].expand();
  for (int64_t a : addresses) {
    for (int64_t s : strides) {
      CompilerHandle ch;
      EXPECT_NO_THROW(b->emit_kernel(ch.c, make_params(a, s))) << "addresses=" << a << " stride_bytes=" << s;
    }
  }
}

// 0 sp/sp, 1 gpr/gpr, 2 sp store + gpr load, 3 gpr store + sp load.
// Forms 2 and 3 address identical bytes through a differently-named
// register, which is what separates a name match from an address match.
TEST(StoreLoadFootprint, BaseRegAxisCoversAllFourAddressingForms) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  const std::vector<int64_t> expected{0, 1, 2, 3};
  EXPECT_EQ(b->axes()[2].expand(), expected);
}

TEST(StoreLoadFootprint, ExposesAluOpsOption) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  auto opts = b->options();
  ASSERT_EQ(opts.size(), 1u);
  const auto* alu = find_option(opts, "alu_ops");
  ASSERT_NE(alu, nullptr);
  EXPECT_EQ(alu->default_value, 1);
}

// ---- Normalization ----

// The address rotation repeats to fill a constant number of links, so
// per-link cost stays comparable across the whole `addresses` axis
// instead of drowning in loop overhead at the small end.
TEST(StoreLoadFootprint, SitesPerKernelIsConstantAcrossTheAddressesAxis) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  for (int64_t a : {1, 2, 4, 16, 64, 256}) {
    EXPECT_EQ(b->sites_per_kernel(make_params(a, 8)), kUnrollLinks) << "addresses=" << a;
  }
}

// The loop body is capped in instructions as well as links, so a large
// alu_ops must shrink the unroll rather than growing the code. Without
// this, alu_ops=64 would emit 132 KB of loop body and start competing
// with the instruction cache.
TEST(StoreLoadFootprint, LoopBodyIsCappedInInstructionsNotJustLinks) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  for (int64_t alu : {1, 4, 16, 32, 64}) {
    size_t links = b->sites_per_kernel(make_params(64, 8, /*base_reg=*/0, alu));
    size_t body = links * (2 + static_cast<size_t>(alu));
    EXPECT_LE(body, 8192u) << "alu_ops=" << alu << " emits " << body << " instructions";
  }
}

// The cap must not disturb the default sweep, which runs at alu_ops=1.
TEST(StoreLoadFootprint, DefaultAluOpsKeepsTheFullUnroll) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  for (int64_t a : {1, 2, 4, 16, 64, 256}) {
    EXPECT_EQ(b->sites_per_kernel(make_params(a, 8)), kUnrollLinks) << "addresses=" << a;
  }
}

// Past kUnrollLinks the rotation no longer fits, so one rotation is the
// floor and sites grow with `addresses`.
TEST(StoreLoadFootprint, SitesPerKernelFallsBackToOneRotationWhenAddressesExceedUnroll) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->sites_per_kernel(make_params(1024, 8)), 1024u);
}

TEST(StoreLoadFootprint, IterationsAmortizesAtOpBudget) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  EXPECT_EQ(b->iterations(make_params(64, 8)), 20'000'000u / kUnrollLinks);
  EXPECT_EQ(b->iterations(make_params(1, 8)), 20'000'000u / kUnrollLinks);
}

// ---- Parameter rejection ----

TEST(StoreLoadFootprint, RejectsZeroAddresses) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(0, 8)), std::invalid_argument);
}

TEST(StoreLoadFootprint, RejectsStrideBelowOneWord) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(4, 4)), std::invalid_argument);
}

TEST(StoreLoadFootprint, RejectsStrideNotAWordMultiple) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(4, 12)), std::invalid_argument);
}

TEST(StoreLoadFootprint, RejectsBaseRegOutsideTheFourForms) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  for (int64_t bad : {-1, 4}) {
    CompilerHandle ch;
    EXPECT_THROW(b->emit_kernel(ch.c, make_params(4, 8, bad)), std::invalid_argument) << "base_reg=" << bad;
  }
}

// Past the addressing window sljit would re-base with an extra
// instruction per link, silently inflating the measured latency. The
// parameter point is rejected instead.
TEST(StoreLoadFootprint, RejectsOffsetPastAddressingWindow) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  CompilerHandle ch;
  EXPECT_THROW(b->emit_kernel(ch.c, make_params(512, 128)), std::invalid_argument);
}

// ---- Codegen ----

// The widest default point, both addressing modes. verify_layout is the
// oracle: it throws unless every link is exactly one store, one load and
// alu_ops ALU instructions, which is what proves sljit encoded each
// offset in a single instruction.
TEST(StoreLoadFootprint, EmitsUniformLinksAtTheWidestDefaultPoint) {
  for (int64_t base_reg : {0, 1, 2, 3}) {
    auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
    ASSERT_NE(b, nullptr);
    ferret::JittedKernel k(*b, make_params(256, 128, base_reg));
    EXPECT_TRUE(k.ok()) << "base_reg=" << base_reg;
  }
}

// Zero ALU ops would force the load to target the store's own source
// register, which changes the stored value into a constant and makes a
// same-register peephole look like fast forwarding. That is a different
// experiment, so it is rejected rather than silently measured.
TEST(StoreLoadFootprint, RejectsAluOpsBelowOne) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  for (int64_t alu : {0, -1}) {
    CompilerHandle ch;
    EXPECT_THROW(b->emit_kernel(ch.c, make_params(4, 8, /*base_reg=*/0, alu)), std::invalid_argument)
        << "alu_ops=" << alu;
  }
}

TEST(StoreLoadFootprint, EmittedKernelRuns) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  ferret::Params p = make_params(4, 8);
  // Keep the smoke run short — iterations() targets 20 M links.
  p.set("addresses", 4);
  ferret::JittedKernel k(*b, p);
  ASSERT_TRUE(k.ok());
  k.fn()();
}

#if defined(__aarch64__) || defined(_M_ARM64)
// Both points emit exactly kUnrollLinks links with an identical
// prologue, epilogue and loop scaffold, so the whole code-size delta is
// one extra 4-byte ADD per link. This is what makes alu_ops usable as a
// cycle-accounting calibration: one more op must mean one more
// instruction, nothing else.
TEST(StoreLoadFootprint, EachAluOpAddsExactlyOneInstructionPerLink) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  ferret::JittedKernel one(*b, make_params(64, 8, /*base_reg=*/0, /*alu_ops=*/1));
  ASSERT_TRUE(one.ok());
  ferret::JittedKernel two(*b, make_params(64, 8, /*base_reg=*/0, /*alu_ops=*/2));
  ASSERT_TRUE(two.ok());
  EXPECT_EQ(two.code_size() - one.code_size(), kUnrollLinks * 4u);
}

// Selecting the general-register base hoists one sljit_get_local_base
// out of the loop and leaves the link body byte-identical, so the two
// modes differ by a bounded constant rather than by per-link work.
TEST(StoreLoadFootprint, BaseRegisterChoiceDoesNotChangePerLinkCode) {
  auto b = ferret::BenchmarkRegistry::create("store_load_footprint");
  ASSERT_NE(b, nullptr);
  ferret::JittedKernel sp(*b, make_params(256, 8, /*base_reg=*/0));
  ASSERT_TRUE(sp.ok());
  ferret::JittedKernel gpr(*b, make_params(256, 8, /*base_reg=*/1));
  ASSERT_TRUE(gpr.ok());
  size_t delta = gpr.code_size() - sp.code_size();
  EXPECT_LE(delta, 16u) << "general-register base added per-link work, not just a hoisted base setup";
}
#endif

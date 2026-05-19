#pragma once

#include <cassert>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <vector>

// emit_outer_loop is a function template; its body calls sljit_emit_*
// at instantiation time, so this header must pull in the full sljit
// declarations rather than forward-declaring `struct sljit_compiler`
// the way benchmark.hpp does. Treat bench_helpers.hpp as a JIT-only
// header — do not include it from non-JIT public headers.
extern "C" {
#include <sljitLir.h>
}

namespace ferret {

// Emits the canonical ferret outer-loop scaffold around a body lambda:
//
//   MOV counter_reg, iters
//   loop_top:
//     [body()]
//   SUB|SET_Z counter_reg, counter_reg, 1
//   JNZ loop_top
//
// `counter_reg` must be a scratch register the body neither reads nor
// writes; the body sees it at the value (iters - i) on iteration i but
// most callers ignore that. `iters` >= 1; iters == 1 still emits the
// loop (the JNZ falls through after one decrement) — callers that want
// to skip the scaffold entirely for the iters==0 fast path should guard
// the call themselves.
template <typename Body>
void emit_outer_loop(sljit_compiler* c, sljit_s32 counter_reg, size_t iters, Body emit_body) {
  assert(iters > 0);
  sljit_emit_op1(c, SLJIT_MOV, counter_reg, 0, SLJIT_IMM, static_cast<sljit_sw>(iters));
  sljit_label* loop_top = sljit_emit_label(c);
  emit_body();
  sljit_emit_op2(c, SLJIT_SUB | SLJIT_SET_Z, counter_reg, 0, counter_reg, 0, SLJIT_IMM, 1);
  sljit_jump* back = sljit_emit_jump(c, SLJIT_NOT_ZERO);
  sljit_set_label(back, loop_top);
}

// Returns the per-kernel outer-loop iteration count that amortizes the
// runner's tick-read overhead to a target total work budget. The total
// per-rep work is approximately `target_ops`; per-site cost is
// total / (sites * iters). Floored at 1 so a kernel always runs once.
inline size_t compute_iterations(size_t target_ops, size_t sites_per_kernel) {
  if (sites_per_kernel == 0) {
    return 1;
  }
  size_t n = target_ops / sites_per_kernel;
  return n == 0 ? 1 : n;
}

// Asserts every label in `labels` sits at base + i * spacing relative to
// labels[0]. When `strict` is true (direct_branch_footprint), the actual
// offset must equal expected exactly. When false (branch_history_footprint),
// the actual offset must be >= expected (sites may overshoot when sljit
// picks a longer encoding; spacing is a floor, not an exact stride).
// Throws std::runtime_error on the first mismatch with the per-site delta.
// No-op when `labels.size() < 2`.
//
// `context` is prepended to the error message so the user sees which
// benchmark complained.
inline void verify_uniform_spacing(const std::vector<sljit_label*>& labels, size_t spacing, bool strict,
                                   const char* context = "") {
  if (labels.size() < 2) {
    return;
  }
  size_t base = sljit_get_label_addr(labels[0]);
  for (size_t i = 1; i < labels.size(); ++i) {
    size_t addr = sljit_get_label_addr(labels[i]);
    size_t actual = addr - base;
    size_t expected = i * spacing;
    const bool ok = strict ? (actual == expected) : (actual >= expected);
    if (!ok) {
      std::string msg;
      if (*context != '\0') {
        msg.append(context).append(": ");
      }
      msg.append("site ").append(std::to_string(i)).append(" at offset ").append(std::to_string(actual));
      if (strict) {
        msg.append(", expected ").append(std::to_string(expected));
        msg.append(" (delta ").append(std::to_string(static_cast<int64_t>(actual) - static_cast<int64_t>(expected))).append(")");
      } else {
        msg.append(", expected at least ").append(std::to_string(expected));
      }
      throw std::runtime_error(msg);
    }
  }
}

}  // namespace ferret

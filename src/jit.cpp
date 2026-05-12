#include "ferret/jit.hpp"

extern "C" {
#include <sljitLir.h>
}

#include <utility>

namespace ferret {

JittedKernel::JittedKernel(Benchmark& b, const Params& p) {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  if (c == nullptr) {
    return;
  }
  b.emit_kernel(c, p);
  if (sljit_get_compiler_error(c) != SLJIT_SUCCESS) {
    sljit_free_compiler(c);
    return;
  }
  void* code = sljit_generate_code(c, 0, nullptr);
  // sljit_get_generated_code_size must be called on the still-live compiler.
  size_t code_size = sljit_get_generated_code_size(c);
  if (code != nullptr) {
    // Must run before sljit_free_compiler — label addresses are populated
    // by sljit_generate_code and freed by sljit_free_compiler, and
    // benchmarks that patch hand-emitted instructions need
    // sljit_get_executable_offset(c) to reach the writable mapping.
    b.verify_layout(c);
  }
  sljit_free_compiler(c);
  code_ = code;
  code_size_ = code_size;
}

JittedKernel::~JittedKernel() {
  if (code_ != nullptr) {
    sljit_free_code(code_, nullptr);
  }
}

JittedKernel::JittedKernel(JittedKernel&& other) noexcept : code_(other.code_) { other.code_ = nullptr; }

JittedKernel& JittedKernel::operator=(JittedKernel&& other) noexcept {
  if (this != &other) {
    if (code_ != nullptr) {
      sljit_free_code(code_, nullptr);
    }
    code_ = other.code_;
    other.code_ = nullptr;
  }
  return *this;
}

JittedKernel::fn_t JittedKernel::fn() const noexcept { return reinterpret_cast<fn_t>(code_); }

}  // namespace ferret

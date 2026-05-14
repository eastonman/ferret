#include "ferret/jit.hpp"

extern "C" {
#include <sljitLir.h>
}

#include <memory>
#include <utility>

namespace ferret {

namespace {

// RAII deleters so that sljit_compiler and the generated executable
// code are freed even when emit_kernel or verify_layout throws.
// Without these, those two raw allocations leak on the exception path —
// LSan catches this when ferret is built with -fsanitize=address.
struct SljitCompilerDeleter {
  void operator()(sljit_compiler* c) const noexcept { sljit_free_compiler(c); }
};
using CompilerPtr = std::unique_ptr<sljit_compiler, SljitCompilerDeleter>;

struct SljitCodeDeleter {
  void operator()(void* p) const noexcept { sljit_free_code(p, nullptr); }
};
using CodePtr = std::unique_ptr<void, SljitCodeDeleter>;

}  // namespace

JittedKernel::JittedKernel(Benchmark& b, const Params& p) {
  CompilerPtr c(sljit_create_compiler(nullptr));
  if (c == nullptr) {
    return;
  }
  b.emit_kernel(c.get(), p);  // may throw — compiler freed via CompilerPtr
  if (sljit_get_compiler_error(c.get()) != SLJIT_SUCCESS) {
    return;
  }
  CodePtr code(sljit_generate_code(c.get(), 0, nullptr));
  // sljit_get_generated_code_size must be called on the still-live compiler.
  size_t code_size = sljit_get_generated_code_size(c.get());
  if (code == nullptr) {
    return;
  }
  // Must run before sljit_free_compiler — label addresses are populated
  // by sljit_generate_code and freed by sljit_free_compiler, and
  // benchmarks that patch hand-emitted instructions need
  // sljit_get_executable_offset(c) to reach the writable mapping. May
  // throw; CodePtr/CompilerPtr clean up.
  b.verify_layout(c.get());
  code_ = code.release();
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

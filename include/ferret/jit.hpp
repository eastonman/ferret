#pragma once

#include <cstddef>

#include "ferret/benchmark.hpp"
#include "ferret/params.hpp"

namespace ferret {

// RAII handle for a sljit-emitted kernel. On construction, runs the
// benchmark's emit_kernel and generates machine code. On failure
// (compiler alloc, sljit error, or code-gen failure) ok() returns false
// and the destructor is a no-op. Move-only.
class JittedKernel {
 public:
  JittedKernel() = default;
  JittedKernel(Benchmark& b, const Params& p);
  ~JittedKernel();

  JittedKernel(JittedKernel&&) noexcept;
  JittedKernel& operator=(JittedKernel&&) noexcept;
  JittedKernel(const JittedKernel&) = delete;
  JittedKernel& operator=(const JittedKernel&) = delete;

  [[nodiscard]] bool ok() const noexcept { return code_ != nullptr; }

  using fn_t = void (*)();
  [[nodiscard]] fn_t fn() const noexcept;  // precondition: ok()

  // Size of the generated machine code in bytes. 0 when !ok().
  [[nodiscard]] size_t code_size() const noexcept { return code_size_; }

 private:
  void* code_ = nullptr;
  size_t code_size_ = 0;
};

}  // namespace ferret

#pragma once

#include <string>

extern "C" {
#include <sljitLir.h>
}

#include "ferret/benchmark.hpp"

namespace ferret::testing {

inline const BenchOption* find_option(const BenchOptions& opts, const std::string& name) {
  for (const auto& o : opts) {
    if (o.name == name) return &o;
  }
  return nullptr;
}

struct CompilerHandle {
  sljit_compiler* c = sljit_create_compiler(nullptr);
  CompilerHandle() = default;
  ~CompilerHandle() {
    if (c) sljit_free_compiler(c);
  }
  CompilerHandle(const CompilerHandle&) = delete;
  CompilerHandle& operator=(const CompilerHandle&) = delete;
};

}  // namespace ferret::testing

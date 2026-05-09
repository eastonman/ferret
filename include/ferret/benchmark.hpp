#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ferret/axis.hpp"
#include "ferret/params.hpp"

// Forward-declare sljit_compiler so headers don't pull in sljitLir.h
// transitively. Implementations include sljitLir.h directly.
struct sljit_compiler;

namespace ferret {

class Benchmark {
public:
  virtual ~Benchmark() = default;
  virtual std::string name() const = 0;
  virtual SweepAxes axes() const = 0;
  virtual size_t sites_per_kernel(const Params& p) const = 0;
  virtual size_t iterations(const Params& p) const = 0;
  virtual void emit_kernel(sljit_compiler* c, const Params& p) = 0;
};

class BenchmarkRegistry {
public:
  using Factory = std::function<std::unique_ptr<Benchmark>()>;

  static void register_benchmark(std::string name, Factory factory);
  static std::unique_ptr<Benchmark> create(const std::string& name);
  static std::vector<std::string> names();
};

}  // namespace ferret

// Registers a Benchmark subclass under a string name. Place at file
// scope in any .cpp under benchmarks/.
#define FERRET_BENCHMARK(NAME, CLASS)                                    \
  namespace {                                                            \
  const bool _ferret_register_##CLASS = []() {                           \
    ::ferret::BenchmarkRegistry::register_benchmark(                     \
        NAME, []() {                                                     \
          return std::unique_ptr<::ferret::Benchmark>(new CLASS());      \
        });                                                              \
    return true;                                                         \
  }();                                                                   \
  }

# `main.cpp` Decomposition — Design Spec

**Date:** 2026-05-12
**Status:** Design — pending implementation plan
**Baseline:** `bf5f00f` (`main`)
**Working branch:** `refactor/cleanup`

## Goal

Decompose `src/main.cpp` (360 lines) so every translation unit has one
responsibility, both clang-tidy `NOLINT` suppressions on `do_run`/`main`
disappear, and `do_run` becomes reviewable in one screen. Readability is
an explicit secondary goal: the moved code receives a comment-debt sweep
to remove round-N narration in favor of timeless invariants.

## Motivation

Concrete signals on the current `main.cpp`:

- 360 lines — by far the largest source file in the project (next is
  `src/axis.cpp` at 90).
- `do_run` is 153 lines (`main.cpp:119–271`), 8 parameters, suppresses
  `readability-function-cognitive-complexity` and
  `bugprone-easily-swappable-parameters`.
- `main` suppresses `bugprone-exception-escape`.
- Mixes four unrelated responsibilities: sljit kernel lifecycle, frequency
  string parsing, CLI extras tokenization + classification, and end-to-end
  benchmark orchestration.
- Manual `jit_free` calls in the catch handler of `do_run` (`main.cpp:256`)
  exist only because the kernel is not RAII.

## Non-goals

These are explicitly out of scope so the diff stays focused:

- **No behavior change.** Error messages, exit codes, CSV format, axis and
  option semantics, log-level behavior — bit-for-bit unchanged. The
  refactor must be transparent to existing integration tests.
- **No public-API churn for benchmark authors.** `Benchmark`,
  `BenchmarkRegistry`, `FERRET_BENCHMARK`, `BenchOptions`, `Params` are
  not modified.
- **No fix for the `seed` vs. `BenchOptions` asymmetry.** `seed` is
  currently a hard-coded magic option (special-cased in `inject_options`
  and `column_names`). Modeling it as a default `BenchOption` is a
  follow-up, not part of this refactor.
- **No removal of defense-in-depth duplications.** Per project policy,
  CLI pre-flight checks and library-internal throws both remain.
- **No comment cleanup outside the files this refactor touches.**

## Target file layout

```
include/ferret/
  jit.hpp           [new]    RAII JittedKernel
  freq.hpp          [new]    parse_freq
  cli_axis.hpp      [+]      parse_option_value, parse_extras
  run_command.hpp   [new]    RunOptions, run, list_command
src/
  jit.cpp           [new]
  freq.cpp          [new]
  cli_axis.cpp      [+]      two new functions
  run_command.cpp   [new]    decomposed do_run body
  main.cpp          [shrunk] ~360 → ~120 lines, CLI11 wiring only
tests/
  test_freq.cpp     [new]    parse_freq units
  test_jit.cpp      [new]    JittedKernel RAII semantics
  test_cli_axis.cpp [+]      parse_option_value + parse_extras cases
```

## Module APIs

### `include/ferret/jit.hpp`

```cpp
#pragma once
#include "ferret/benchmark.hpp"
#include "ferret/params.hpp"

namespace ferret {

class JittedKernel {
 public:
  JittedKernel() = default;
  JittedKernel(Benchmark& b, const Params& p);
  ~JittedKernel();

  JittedKernel(JittedKernel&&) noexcept;
  JittedKernel& operator=(JittedKernel&&) noexcept;
  JittedKernel(const JittedKernel&) = delete;
  JittedKernel& operator=(const JittedKernel&) = delete;

  bool ok() const noexcept { return code_ != nullptr; }
  using fn_t = void (*)();
  fn_t fn() const noexcept;       // precondition: ok()

 private:
  void* code_ = nullptr;
};

}  // namespace ferret
```

Constructor runs `sljit_create_compiler` + `emit_kernel` + `sljit_generate_code`
inline. Failure modes (null compiler, `sljit_get_compiler_error`, null code)
collapse to `ok() == false`; no exceptions. Destructor calls
`sljit_free_code` when `code_` is non-null. Move operations transfer
ownership.

### `include/ferret/freq.hpp`

```cpp
#pragma once
#include <string>

namespace ferret {
// Parses "4.521GHz" / "100MHz" / "1.2e9Hz" / bare numbers (Hz).
// Throws std::invalid_argument on empty numeric, trailing junk after the
// number, non-finite (NaN, +/-inf), or non-positive values.
// Returned value is hertz.
double parse_freq(const std::string& s);
}  // namespace ferret
```

Semantics and exception messages identical to the current `main.cpp:33`.

### `include/ferret/cli_axis.hpp` (additions)

```cpp
// existing:
//   std::vector<int64_t> parse_cli_axis_value(const std::string&, const Axis&);

// New. Parses one --<option>=<value> scalar (integer).
// Throws std::invalid_argument on non-integer or trailing junk.
int64_t parse_option_value(const std::string& v);

// New. Turns CLI11 allow_extras() remainder into a name -> value map.
// Each token must be --name=value. Throws std::invalid_argument otherwise
// with the same messages as the current inlined loop:
//   "unexpected argument: <tok>"
//   "--axis flags must be --name=value: <tok>"
std::map<std::string, std::string> parse_extras(const std::vector<std::string>& tokens);
```

### `include/ferret/run_command.hpp`

```cpp
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>

namespace ferret {

struct RunOptions {
  std::string name;
  std::map<std::string, std::string> overrides;  // already-tokenized extras
  std::string out_path;                          // empty = stdout
  int core = -1;                                 // <0 = no pin
  std::optional<double> freq_hz;                 // enables cycle columns
  int reps = 7;
  int warmup = 1;
  int64_t seed = 1;
};

// Runs one benchmark sweep end-to-end. Returns process exit code
// (0 success, 2 user/parameter error). Never throws across the boundary.
int run(const RunOptions& opts);

// Lists registered benchmark names on stdout. Always returns 0.
int list_command();

}  // namespace ferret
```

### `src/run_command.cpp` internal helpers

The body of `run` is split into static-local helpers, not exported:

| Helper             | Job                                                                                | ~lines |
|--------------------|------------------------------------------------------------------------------------|-------:|
| `classify_overrides` | partition `opts.overrides` into axis-value map + option-value map; errors on unknown names | 25 |
| `inject_options`     | set default option values + `seed` into every expanded `Params` row              | 10 |
| `apply_pinning`      | core pin + priority boost + mlockall; warn on each failure                       | 10 |
| `open_output`        | open `ofstream` if `out_path` set, else use `cout`                               | 10 |
| `column_names`       | build axis-name vector: axes + options + `"seed"`                                 | 10 |
| `measure_all`        | per-row loop using `JittedKernel` RAII; returns buffered `(Params, MeasurementRow)` vector or error code | 30 |
| `emit_csv`           | header + rows + flush                                                             | 10 |

`run` itself becomes ~20 lines of top-level glue calling these helpers in
sequence. No helper exceeds ~30 lines; both `NOLINT` suppressions on
`do_run`/`main` are removed.

## `src/main.cpp` final shape

After all extractions, `main.cpp` is CLI11 wiring + dispatch only:

```cpp
int main(int argc, char** argv) {
  flog::init();

  CLI::App app{"ferret — frontend reverse-engineering toolkit"};
  app.require_subcommand(1);
  // ...subcommand + option declarations as today...

  CLI11_PARSE(app, argc, argv);
  flog::set_level(flog::parse_level(log_level_str));

  if (*list_cmd) return ferret::list_command();

  if (*run_cmd) {
    if (K < 1)      { flog::error("--reps must be >= 1 (got {})", K);       return 2; }
    if (warmup < 0) { flog::error("--warmup must be >= 0 (got {})", warmup); return 2; }

    ferret::RunOptions opts;
    try { opts.overrides = ferret::parse_extras(run_cmd->remaining()); }
    catch (const std::exception& e) { flog::error("{}", e.what()); return 2; }

    if (!freq_str.empty()) {
      try { opts.freq_hz = ferret::parse_freq(freq_str); }
      catch (const std::exception& e) { flog::error("invalid {}", e.what()); return 2; }
    }
    opts.name = name; opts.out_path = out_path; opts.core = core;
    opts.reps = K; opts.warmup = warmup; opts.seed = seed;
    return ferret::run(opts);
  }
  return 0;
}
```

Target size: ~120 lines, dominated by CLI11 option declarations. Both
`NOLINT` suppressions on `do_run`/`main` are gone.

## Comment-debt sweep

Applied only to code moved in commit 4 (the `do_run` body). Rules per the
project's existing comment policy:

- **Drop** comments that narrate patch history or reference spec sections
  by number when the code itself is self-describing. Example:
  `// Inject non-swept options + seed into every row so they're recorded
  in CSV.` — the function is named `inject_options`; delete.
- **Drop** comments that narrate control flow visible in the next few
  lines.
- **Keep** one-line invariants describing non-obvious failure modes.
  Example: `// non-finite tpns would propagate NaN into CSV.` — keep.
- **Keep** comments documenting non-obvious external-tool quirks.
  Example: `// --log-level is on run_cmd, not app, because run_cmd uses
  allow_extras() and would otherwise swallow it as an --<axis>=
  override.` — keep.
- **Rewrite** "Spec §7 class-1: benchmark-parameter errors must produce
  no partial output. Collect every row's measurement in memory and emit
  only on full success." → `// no partial output: buffer all rows, emit
  only after every row succeeded.`

## Testing strategy

Pure decomposition: every existing test passes unchanged after each
commit. Integration tests in `tests/test_integration.cpp` cover the
end-to-end behavior the refactor must not perturb.

New unit tests:

- `tests/test_freq.cpp` — `parse_freq` cases: GHz/MHz/kHz/Hz suffix, bare
  number, empty numeric component, trailing junk, scientific notation,
  NaN, +/-inf, zero, negative.
- `tests/test_jit.cpp` — `JittedKernel` RAII: default-constructed
  `ok() == false`; constructed from a trivial test-only benchmark whose
  `emit_kernel` emits `SLJIT_RETURN_VOID` produces `ok() == true`;
  move-constructed kernel's source becomes `!ok()`; the resulting
  function pointer from `fn()` is callable and returns. Destructor
  correctness is exercised indirectly by running the move/destroy/recreate
  cycle a few hundred times under the standard test build — any leak or
  double-free would surface as an ASan/MSan failure in CI.
- `tests/test_cli_axis.cpp` (extend) —
  `parse_option_value` ("42" / "abc" / "42x" / empty), `parse_extras`
  (mixed well-formed map, "--name=value" with empty value, lone "-",
  positional fragment without `--` prefix, "--name" missing `=`).

All new gtest binaries register via
`gtest_discover_tests(... PROPERTIES TIMEOUT 30)` per the project's
test-harness timeout policy. No `timeout N` shell prefixes.

CI matrix (Linux x86_64, Linux aarch64, macOS, Nix devshell) must stay
green at every commit.

## Migration order

Four commits, each compiles and passes `ctest` in isolation. Commit
subjects follow the repo's Angular convention.

1. **`refactor(jit): extract sljit kernel lifecycle into ferret::JittedKernel`**
   New `include/ferret/jit.hpp` + `src/jit.cpp`. `main.cpp` uses
   `JittedKernel` directly; the manual `jit_free` call in the catch
   handler is removed (RAII handles it). New `tests/test_jit.cpp`.

2. **`refactor(freq): extract parse_freq into ferret/freq`**
   New `include/ferret/freq.hpp` + `src/freq.cpp`. `main.cpp` uses
   `ferret::parse_freq`. New `tests/test_freq.cpp`.

3. **`refactor(cli_axis): centralize option value and extras parsing`**
   `parse_option_value` and `parse_extras` added to `cli_axis.{hpp,cpp}`.
   `main.cpp` calls both; the inlined extras-tokenization loop is gone.
   `tests/test_cli_axis.cpp` extended.

4. **`refactor(run_command): move do_run into ferret::run with helper decomposition`**
   New `include/ferret/run_command.hpp` + `src/run_command.cpp`. `do_run`
   body becomes `ferret::run`; helpers per the table above. `do_list`
   becomes `ferret::list_command`. Comment-debt sweep applied on the
   moved code. Both `NOLINT` suppressions on `do_run`/`main` removed.

## Follow-ups (not in this refactor)

- Model `seed` as a default `BenchOption` so `inject_options` and
  `column_names` no longer special-case it. Touches `run_command.cpp`
  and the `seed`-consuming benchmark (`direct_branch_footprint`); needs
  a separate spec because it changes the per-benchmark options data
  model.

## Acceptance criteria

- `src/main.cpp` ≤ 130 lines.
- No `NOLINTNEXTLINE` / `NOLINTBEGIN` suppressions remain in
  `main.cpp` or `run_command.cpp`.
- All pre-existing tests pass on each of the four commits.
- New unit tests added per the testing strategy above.
- `clang-format` and `clang-tidy` clean on all touched files.
- CI green on Linux x86_64, Linux aarch64, macOS, and the Nix job at
  every commit.
- Equivalence check: run
  `ferret run direct_branch_footprint --branches=1,2,4,8 --spacing_bytes=64
   --reps=3 --warmup=1 --seed=1 --freq=4.0GHz --out=/tmp/ferret-<sha>.csv`
  on the `main` baseline and on the post-refactor branch. The CSV header
  line and every non-timing column (param values, axes, options, seed)
  must be byte-identical between the two runs; timing columns are
  expected to differ due to runtime noise and are not compared.
  Verification via `diff <(cut -d, -f1-N pre.csv) <(cut -d, -f1-N post.csv)`
  where `N` is the number of leading non-timing columns reported by the
  header.

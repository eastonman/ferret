# Contributing

The operational checklist — what to run before opening a PR, commit
message conventions, branch naming, CI behavior, footguns — lives in
[`../AGENTS.md`](../AGENTS.md). Both human contributors and agentic
workers should treat that as the source of truth.

This page is a brief orientation:

## Formatters and linters

Formatters and linters run in CI and must pass before merging.

- C++: `clang-format` (style in `.clang-format`) and `clang-tidy`
  (checks in `.clang-tidy`, `WarningsAsErrors: '*'`).
- Python: `ruff format` and `ruff check` (config in `pyproject.toml`,
  requires `ruff>=0.14`).

Apply formatters locally:

```sh
./scripts/format.sh
```

Verify the way CI does (this is the gate — `lint.yml` runs `lint.sh`
verbatim):

```sh
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
./scripts/lint.sh
```

All tools are provided by `nix develop`. There are no pre-commit
hooks; the only way to catch a CI lint failure before pushing is to
run `lint.sh` locally.

## Adding a benchmark

See [`writing-a-benchmark.md`](writing-a-benchmark.md) for the
`Benchmark` vtable (six pure virtuals plus the optional
`verify_layout`), the registration macro, the `bench_helpers.hpp`
JIT-time utilities, and worked examples covering the frequency-probe
and parameter-sweep patterns.

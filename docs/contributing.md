# Contributing

## Formatting and linting

Formatters and linters run in CI and must pass before merging.

- C++: `clang-format` (style in `.clang-format`) and `clang-tidy`
  (checks in `.clang-tidy`).
- Python: `ruff format` and `ruff check` (config in `pyproject.toml`).

Apply formatters locally:

```sh
./scripts/format.sh
```

Verify the way CI does:

```sh
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
./scripts/lint.sh
```

All tools are provided by `nix develop`.

## Adding a benchmark

See [`writing-a-benchmark.md`](writing-a-benchmark.md) for the
`Benchmark` vtable, the registration macro, and worked examples
covering the frequency-probe and parameter-sweep patterns.

#!/usr/bin/env bash
# Read-only formatting + lint checks. Called verbatim by CI.
# Returns non-zero on any violation.

set -euo pipefail

cd "$(dirname "$0")/.."

# C++ files to format-check (includes tests).
mapfile -t CXX_FORMAT_FILES < <(
  find src include benchmarks tests \
    \( -name '*.cpp' -o -name '*.hpp' \) \
    -type f
)

# C++ files to lint with clang-tidy (excludes tests — gtest macros).
mapfile -t CXX_LINT_FILES < <(
  find src benchmarks -name '*.cpp' -type f
)

echo "==> clang-format --dry-run"
clang-format --dry-run --Werror "${CXX_FORMAT_FILES[@]}"

echo "==> ruff format --check"
ruff format --check scripts/

echo "==> ruff check"
ruff check scripts/

if [ ! -f build/compile_commands.json ]; then
  echo "lint.sh: build/compile_commands.json missing." >&2
  echo "        Run: cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
  exit 1
fi

echo "==> clang-tidy"
clang-tidy --quiet -p build "${CXX_LINT_FILES[@]}"

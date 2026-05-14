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
ruff format --check scripts/ tests/python/

echo "==> ruff check"
ruff check scripts/ tests/python/

if [ ! -f build/compile_commands.json ]; then
  echo "lint.sh: build/compile_commands.json missing." >&2
  echo "        Run: cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
  exit 1
fi

echo "==> clang-tidy"
# clang-tidy from nixpkgs is unwrapped, so the C++ stdlib search paths
# that the clang++ wrapper injects (NIX_CFLAGS_COMPILE plus an implicit
# -cxx-isystem) must be passed through explicitly. Empty on non-Nix → no-op.
TIDY_EXTRA_ARGS=()
if [ -n "${NIX_CFLAGS_COMPILE:-}" ]; then
  for flag in $NIX_CFLAGS_COMPILE; do
    TIDY_EXTRA_ARGS+=(--extra-arg="$flag")
  done
  for flag in $NIX_CFLAGS_COMPILE; do
    if [[ "$flag" == *libcxx*/include ]]; then
      TIDY_EXTRA_ARGS+=(--extra-arg="-isystem$flag/c++/v1")
      break
    fi
  done
fi
clang-tidy --quiet -p build "${TIDY_EXTRA_ARGS[@]}" "${CXX_LINT_FILES[@]}"

#!/usr/bin/env bash
# Apply clang-format and ruff in place across the repo.
# Idempotent. Safe to run repeatedly.

set -euo pipefail

cd "$(dirname "$0")/.."

# C++ files: format everything under these trees.
mapfile -t CXX_FILES < <(
  find src include benchmarks tests \
    \( -name '*.cpp' -o -name '*.hpp' \) \
    -type f
)

if [ "${#CXX_FILES[@]}" -gt 0 ]; then
  clang-format -i "${CXX_FILES[@]}"
fi

# Python: format then autofix lint findings.
ruff format scripts/
ruff check --fix scripts/

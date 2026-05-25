#!/usr/bin/env bash
# Apply clang-format, ruff, cmake-format, and prettier in place across
# the repo. Idempotent. Safe to run repeatedly.

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
ruff format scripts/ tests/python/
ruff check --fix scripts/ tests/python/

# CMake: format CMakeLists.txt and any *.cmake modules in-tree.
mapfile -t CMAKE_FILES < <(
  find . \
    \( -path ./build -o -path ./.git -o -path ./_deps \) -prune -o \
    \( -name 'CMakeLists.txt' -o -name '*.cmake' \) -type f -print
)
if [ "${#CMAKE_FILES[@]}" -gt 0 ]; then
  cmake-format -i "${CMAKE_FILES[@]}"
fi

# Markdown: prettier honours .prettierignore (excludes docs/superpowers).
prettier --write '**/*.md'

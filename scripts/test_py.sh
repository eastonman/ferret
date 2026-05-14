#!/usr/bin/env bash
# Run the ferret_plot pytest suite from the repo root.
# Returns non-zero on any test failure.

set -euo pipefail

cd "$(dirname "$0")/.."

exec python3 -m pytest tests/python "$@"

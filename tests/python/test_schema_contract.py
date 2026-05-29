"""Cross-language CSV schema contract.

The ferret C++ binary (src/csv.cpp) is the authority for the CSV column
schema. Python (ferret_plot.columns.METADATA_COLS) and the shell
validator (scripts/run_benchmarks.sh) each keep a copy of the metadata
column names. These tests make any drift between the copies a hard
failure instead of a silent plotting bug.
"""

from __future__ import annotations

import os
import re
import subprocess
from pathlib import Path

import pytest
from ferret_plot import columns as cols

ROOT = Path(__file__).resolve().parent.parent.parent
RUNNER = ROOT / "scripts" / "run_benchmarks.sh"

# Metadata columns emitted only when --freq is supplied.
OPTIONAL_FREQ_COLS = frozenset({"cycles_per_site_min", "cycles_per_site_median", "freq_hz"})


def test_metadata_cols_is_exact():
    assert (
        frozenset(
            {
                "benchmark",
                "seed",
                "ticks_min",
                "ticks_median",
                "iters",
                "sites_per_iter",
                "reps",
                "ns_per_site_min",
                "ns_per_site_median",
                "cycles_per_site_min",
                "cycles_per_site_median",
                "freq_hz",
            }
        )
        == cols.METADATA_COLS
    )


def test_shell_validator_columns_are_metadata():
    text = RUNNER.read_text(encoding="utf-8")
    block = text.split("metric_columns = (", 1)[1].split(")", 1)[0]
    names = re.findall(r'"([^"]+)"', block)
    assert names, "could not parse metric_columns from run_benchmarks.sh"
    for name in names:
        assert name in cols.METADATA_COLS, f"{name!r} in run_benchmarks.sh is not in METADATA_COLS"


# direct_branch_footprint's swept axes and benchmark-specific options;
# everything else in its header is metadata.
DBF_AXES = frozenset({"branches", "spacing_bytes", "sattolo_permute"})


def _ferret_bin() -> str:
    """Locate the built ferret binary, or skip if it is not available."""
    candidates = []
    env = os.environ.get("FERRET_BIN")
    if env:
        candidates.append(env)
    candidates.append(str(ROOT / "build" / "ferret"))
    for c in candidates:
        if Path(c).is_file() and os.access(c, os.X_OK):
            return c
    pytest.skip("ferret binary not found (set FERRET_BIN or build build/ferret)")


def _header_columns(extra_args: list[str]) -> set[str]:
    cmd = [
        _ferret_bin(),
        "run",
        "direct_branch_footprint",
        "--branches=1,2,4",
        "--spacing_bytes=64",
        "--reps=1",
        "--warmup=0",
        *extra_args,
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=True)
    header_line = proc.stdout.splitlines()[0]
    return set(header_line.split(","))


def test_binary_header_without_freq_matches_metadata():
    metadata_present = _header_columns([]) - DBF_AXES
    assert metadata_present == cols.METADATA_COLS - OPTIONAL_FREQ_COLS


def test_binary_header_with_freq_matches_metadata():
    metadata_present = _header_columns(["--freq=4.0GHz"]) - DBF_AXES
    assert metadata_present == cols.METADATA_COLS

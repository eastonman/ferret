"""Cross-language CSV schema contract.

The ferret C++ binary (src/csv.cpp) is the authority for the CSV column
schema. Python (ferret_plot.columns.METADATA_COLS) and the shell
validator (scripts/run_benchmarks.sh) each keep a copy of the metadata
column names. These tests make any drift between the copies a hard
failure instead of a silent plotting bug.
"""

from __future__ import annotations

import re
from pathlib import Path

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

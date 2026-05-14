"""Per-benchmark default X/Y/facet column hints.

Keyed off the unique non-NaN value in `df['benchmark']` (or an explicit
--benchmark=NAME override). Lookup misses fall through to ordering
rules in the calling kind; that is not an error.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

import pandas as pd

from ferret_plot.errors import PlotError

XScale = Literal["log", "linear"]


@dataclass(frozen=True)
class BenchmarkDefaults:
    line_x: str | None = None
    line_xscale: XScale | None = None  # None = caller default
    heatmap_x: str | None = None
    heatmap_y: str | None = None
    facet_col: str | None = None


DEFAULTS: dict[str, BenchmarkDefaults] = {
    "direct_branch_footprint": BenchmarkDefaults(
        line_x="branches",
        heatmap_x="branches",
        heatmap_y="spacing_bytes",
    ),
    "dependent_chain_throughput": BenchmarkDefaults(
        line_x="chain_length",
    ),
    "nested_call_depth": BenchmarkDefaults(
        # depth=1..64 is a linear range, not a log2_range — pick linear
        # so 1..64 doesn't render as 1,2,4,8,16,32,64 with everything else
        # squished into log2 space.
        line_x="depth",
        line_xscale="linear",
    ),
}


def detect_benchmark(df: pd.DataFrame, override: str | None) -> str | None:
    """Return the benchmark-name key to look up in DEFAULTS, or None.

    Precedence:
      1. override (from --benchmark=NAME) wins outright.
      2. The unique non-NaN value in df['benchmark'] if the column
         exists, the frame is non-empty, and all non-NaN values agree.
      3. None if the column is absent, the frame is empty, or every
         row is NaN in that column.

    Raises PlotError if df['benchmark'] contains more than one distinct value.
    """
    if override:  # None or empty string => fall through to CSV detection
        return override
    if "benchmark" not in df.columns or len(df) == 0:
        return None
    distinct = df["benchmark"].dropna().unique().tolist()
    if len(distinct) > 1:
        raise PlotError(
            f"mixed-benchmark CSV: found {distinct!r}; pre-filter or pass --benchmark=NAME to force a registry lookup"
        )
    if len(distinct) == 0:
        return None
    return str(distinct[0])


def resolve_defaults(df: pd.DataFrame, override: str | None) -> BenchmarkDefaults:
    """detect_benchmark + DEFAULTS lookup. Unknown benchmarks return
    BenchmarkDefaults() with all-None fields, which is not an error —
    callers fall through to ordering rules."""
    name = detect_benchmark(df, override)
    if name is None:
        return BenchmarkDefaults()
    return DEFAULTS.get(name, BenchmarkDefaults())

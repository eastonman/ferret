"""Column classification and metric resolution.

A ferret CSV has three classes of columns:
- Metadata: emitted by every benchmark (benchmark name, seed, timing,
  iters, sites_per_iter, reps, ns/cycles columns, freq_hz).
- Axes: per-benchmark sweep parameters (e.g. branches, spacing_bytes).
- (Future) Options: per-benchmark scalar options. Today these appear
  alongside axes in the CSV header; this module treats anything
  non-metadata as an axis column.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

import pandas as pd

from ferret_plot.errors import PlotError

MetricChoice = Literal["auto", "cycles", "ns"]
StatChoice = Literal["min", "median"]
_UNKNOWN_BENCH = "ferret"

METADATA_COLS: frozenset[str] = frozenset(
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


@dataclass(frozen=True)
class Metric:
    column: str
    label: str
    short: Literal["cycles", "ns"]


def resolve_metric(df: pd.DataFrame, *, metric: MetricChoice, stat: StatChoice) -> Metric:
    """Resolve the (metric, stat) request to a concrete CSV column.

    metric ∈ {'auto', 'cycles', 'ns'}; stat ∈ {'min', 'median'}.
    'auto' prefers cycles when the cycles column for that stat has
    at least one non-NaN row, else falls back to ns.

    Raises PlotError if the resolved column is missing or all-NaN
    (typically an explicit --metric=cycles on a freq-less CSV).
    """
    if stat not in ("min", "median"):
        raise PlotError(f"stat must be 'min' or 'median' (got {stat!r})")

    cycles_col = f"cycles_per_site_{stat}"
    ns_col = f"ns_per_site_{stat}"

    def _usable(col: str) -> bool:
        return col in df.columns and df[col].notna().any()

    if metric == "auto":
        chosen = cycles_col if _usable(cycles_col) else ns_col
    elif metric == "cycles":
        chosen = cycles_col
    elif metric == "ns":
        chosen = ns_col
    else:
        raise PlotError(f"metric must be 'auto', 'cycles', or 'ns' (got {metric!r})")

    if not _usable(chosen):
        raise PlotError(
            f"metric column {chosen!r} is missing or all-NaN; available columns: {sorted(df.columns.tolist())}"
        )

    short: Literal["cycles", "ns"] = "cycles" if chosen.startswith("cycles_") else "ns"
    label = f"{short} per site ({stat})"
    return Metric(column=chosen, label=label, short=short)


def axis_columns(df: pd.DataFrame) -> list[str]:
    """All non-metadata columns in CSV order."""
    return [c for c in df.columns if c not in METADATA_COLS]


def varying_axis_columns(df: pd.DataFrame) -> list[str]:
    """Axis columns whose values vary across rows.

    A constant-valued axis (e.g. --spacing_bytes=64 sweep with a fixed
    spacing) is an axis in the CSV schema but not interesting to plot.
    """
    return [c for c in axis_columns(df) if df[c].nunique(dropna=False) > 1]


def bench_name(df: pd.DataFrame) -> str:
    """Best-effort benchmark name for plot titles.

    Returns the first non-NaN value of `df['benchmark']` if the column
    exists and the frame is non-empty; otherwise returns a generic
    placeholder. (Strict detection lives in `registry.detect_benchmark`,
    which also validates uniqueness.)
    """
    if "benchmark" in df.columns and len(df):
        return str(df["benchmark"].iloc[0])
    return _UNKNOWN_BENCH

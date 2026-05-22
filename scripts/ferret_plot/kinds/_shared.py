"""Shared helpers for the heatmap-shaped renderers (heatmap, facets, surface)."""

from __future__ import annotations

import argparse

import numpy as np
import pandas as pd
import plotly.colors
import plotly.graph_objects as go

from ferret_plot.columns import varying_axis_columns
from ferret_plot.errors import PlotError
from ferret_plot.formatting import decimate_indices, human_readable
from ferret_plot.registry import BenchmarkDefaults

_MISSING_PREVIEW_LIMIT = 5
DEFAULT_CMAP = "turbo"


def assert_finite_metric(values: np.ndarray, metric_col: str) -> None:
    """Raise PlotError when a metric column contains no finite values."""
    if not np.any(np.isfinite(values)):
        raise PlotError(f"metric column {metric_col!r} contains no finite values; cannot plot")


def assert_logz_positive(values: np.ndarray, *, metric_label: str | None = None) -> None:
    """Raise PlotError when --logz is requested but values contain non-positive entries."""
    finite = values[np.isfinite(values)]
    if len(finite) == 0 or np.any(finite <= 0):
        raise PlotError("--logz requires at least one finite positive value")


def hover_text_grid(  # noqa: PLR0913
    grid: pd.DataFrame,
    *,
    xcol: str,
    ycol: str,
    value_label: str,
    z: np.ndarray,
    transpose: bool = False,
) -> np.ndarray:
    """Build a 2D object array of per-cell hover strings.

    Each cell reads ``"{xcol}={x_val}<br>{ycol}={y_val}<br>{value_label}={z:.3g}"``.
    When ``transpose=False`` (default), the returned shape is ``(n_rows, n_cols)``
    matching the heatmap trace convention.  When ``transpose=True``, the shape
    is ``(n_cols, n_rows)`` to match the surface trace convention where plotly
    indexes text as ``text[x_idx][y_idx]``.
    """
    n_rows, n_cols = z.shape
    if transpose:
        return np.array(
            [
                [
                    f"{xcol}={human_readable(grid.columns[j])}"
                    f"<br>{ycol}={human_readable(grid.index[i])}"
                    f"<br>{value_label}={z[i, j]:.3g}"
                    for i in range(n_rows)
                ]
                for j in range(n_cols)
            ],
            dtype=object,
        )
    return np.array(
        [
            [
                f"{xcol}={human_readable(grid.columns[j])}"
                f"<br>{ycol}={human_readable(grid.index[i])}"
                f"<br>{value_label}={z[i, j]:.3g}"
                for j in range(n_cols)
            ]
            for i in range(n_rows)
        ],
        dtype=object,
    )


def validate_cmap(cmap: str) -> str:
    """Normalise and validate a plotly colorscale name.

    Raises PlotError with a list of valid names on miss.
    """
    normalized = cmap.lower()
    if normalized not in plotly.colors.named_colorscales():
        raise PlotError(
            f"--cmap={cmap!r} is not a valid colorscale; "
            f"valid names: {sorted(plotly.colors.named_colorscales())[:8]}..."
        )
    return normalized


def axis_ticks(labels: list[object]) -> tuple[list[int], list[str]]:
    """Decimate to index positions + human-readable labels.

    Heatmap and surface traces use integer index positions for uniform
    cell spacing (so power-of-2 sweeps don't squish at the low end).
    The layout's tickvals are the same indices, ticktext the value labels.
    """
    kept = decimate_indices(labels)
    return kept, [human_readable(labels[i]) for i in kept]


def resolve_heatmap_xy(
    df: pd.DataFrame,
    args: argparse.Namespace,
    defaults: BenchmarkDefaults,
    *,
    exclude: frozenset[str] = frozenset(),
) -> tuple[str, str]:
    """Pick X/Y axis columns for a heatmap-shaped plot.

    Precedence per axis: explicit CLI flag → registry hint (skipped on
    collision with the other axis or `exclude`) → first varying column
    that doesn't collide. `exclude` lets the facets kind hide the facet
    column so it isn't picked for X or Y.
    """
    varying = [c for c in varying_axis_columns(df) if c not in exclude]
    if len(varying) < 2:  # noqa: PLR2004
        raise PlotError(
            f"heatmap-shaped plot needs at least 2 varying axis columns "
            f"(after exclusions {sorted(exclude)!r}); got {varying!r}"
        )

    x = args.x
    reg_x = defaults.heatmap_x
    if x is None and reg_x is not None and reg_x in varying and reg_x != args.y:
        x = reg_x
    if x is None:
        x = next((c for c in varying if c != args.y), varying[0])
    if x not in df.columns:
        raise PlotError(f"--x={x!r} is not a column in the CSV")

    y = args.y
    reg_y = defaults.heatmap_y
    if y is None and reg_y is not None and reg_y in varying and reg_y != x:
        y = reg_y
    if y is None:
        y = next((c for c in varying if c != x), None)
    if y is None or x == y:
        raise PlotError(f"could not pick distinct X/Y (got x={x!r}, y={y!r})")
    if y not in df.columns:
        raise PlotError(f"--y={y!r} is not a column in the CSV")
    return x, y


def prepare_grid(
    sub_df: pd.DataFrame,
    *,
    xcol: str,
    ycol: str,
    value_col: str,
    require_complete: bool = False,
) -> pd.DataFrame:
    """Pivot one subset to a sorted 2D grid.

    `aggfunc="first"` matches the existing heatmap behavior: duplicate
    (X, Y) rows from concatenated CSVs remain deterministic instead of
    being silently averaged.
    """
    grid = (
        sub_df.pivot_table(
            index=ycol,
            columns=xcol,
            values=value_col,
            aggfunc="first",
            dropna=False,
        )
        .sort_index()
        .sort_index(axis=1)
    )
    if require_complete and grid.isna().to_numpy().any():
        missing = [
            f"({xcol}={x}, {ycol}={y})" for y, row in grid.iterrows() for x, value in row.items() if pd.isna(value)
        ]
        shown = ", ".join(missing[:_MISSING_PREVIEW_LIMIT])
        suffix = "" if len(missing) <= _MISSING_PREVIEW_LIMIT else f", ... ({len(missing)} total)"
        raise PlotError(f"missing grid cells for surface plot: {shown}{suffix}")
    return grid


def build_heatmap_trace(  # noqa: PLR0913
    grid: pd.DataFrame,
    *,
    xcol: str,
    ycol: str,
    value_label: str,
    logz: bool,
    cmap: str,
    cmin: float | None = None,
    cmax: float | None = None,
    coloraxis: str | None = None,
) -> go.Heatmap:
    """Build a `go.Heatmap` trace from a prepared grid.

    Cells are placed at uniform integer index positions so power-of-2
    sweeps don't squish together at the low end — the caller anchors
    tickvals (the same indices) to ticktext labels with the real values
    via `human_readable`. The hover string carries the actual axis
    values so the visual layout and the displayed coordinates stay in
    sync.

    `logz=True` pre-transforms z via np.log10 (plotly's colorscale does
    not accept a log norm directly). NaNs render transparent by default;
    callers can paint a grey background on the layout for "missing"
    cells if they want a visible indicator.

    When `coloraxis` is set, the trace attaches to that shared axis and
    its per-trace colorscale/zmin/zmax are left unset — the caller
    configures them on `layout.coloraxis`.
    """
    z_raw = grid.to_numpy(dtype=float)
    z = np.log10(z_raw) if logz else z_raw
    n_rows, n_cols = z_raw.shape

    hover_text = hover_text_grid(grid, xcol=xcol, ycol=ycol, value_label=value_label, z=z_raw)

    common = dict(
        x=list(range(n_cols)),
        y=list(range(n_rows)),
        z=z,
        text=hover_text,
        hovertemplate="%{text}<extra></extra>",
    )
    if coloraxis is not None:
        return go.Heatmap(**common, coloraxis=coloraxis)
    return go.Heatmap(
        **common,
        colorscale=cmap,
        zmin=cmin,
        zmax=cmax,
        colorbar=dict(title=value_label),
    )

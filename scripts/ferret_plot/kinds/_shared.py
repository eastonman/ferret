"""Shared helpers for the heatmap-shaped renderers (heatmap, facets)."""

from __future__ import annotations

import argparse

import numpy as np
import pandas as pd
from matplotlib.axes import Axes
from matplotlib.colors import Normalize
from matplotlib.image import AxesImage

from ferret_plot.columns import varying_axis_columns
from ferret_plot.errors import PlotError
from ferret_plot.formatting import human_readable
from ferret_plot.registry import BenchmarkDefaults


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


def render_heatmap_cell(  # noqa: PLR0913
    ax: Axes,
    sub_df: pd.DataFrame,
    *,
    xcol: str,
    ycol: str,
    value_col: str,
    norm: Normalize,
) -> AxesImage:
    """Pivot one subset to a 2D grid and draw it as an imshow heatmap.

    `aggfunc="first"` (not the default mean) so duplicate (X, Y) rows
    from concatenated CSVs surface visibly instead of silently averaging.
    NaN cells render as a flat grey fill so absence is distinguishable
    from valid cells anywhere on the viridis scale.
    """
    pivot = (
        sub_df.pivot_table(index=ycol, columns=xcol, values=value_col, aggfunc="first").sort_index().sort_index(axis=1)
    )
    data = np.ma.masked_invalid(pivot.values)
    im = ax.imshow(data, aspect="auto", origin="lower", norm=norm, cmap="viridis")
    im.cmap.set_bad(color="lightgrey")
    ax.set_xticks(range(len(pivot.columns)))
    ax.set_xticklabels([human_readable(v) for v in pivot.columns])
    ax.set_yticks(range(len(pivot.index)))
    ax.set_yticklabels([human_readable(v) for v in pivot.index])
    ax.set_xlabel(xcol)
    ax.set_ylabel(ycol)
    return im

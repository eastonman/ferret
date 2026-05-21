"""2D heatmap renderer (plotly backend) for CSVs with at least 2 varying axis columns."""

from __future__ import annotations

import argparse

import numpy as np
import pandas as pd
import plotly.graph_objects as go

from ferret_plot.columns import bench_name, resolve_metric
from ferret_plot.errors import PlotError
from ferret_plot.kinds._shared import (
    _DEFAULT_CMAP,
    axis_ticks,
    build_heatmap_trace,
    prepare_grid,
    resolve_heatmap_xy,
    validate_cmap,
)
from ferret_plot.registry import resolve_defaults


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> go.Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    xcol, ycol = resolve_heatmap_xy(df, args, defaults)
    grid = prepare_grid(df, xcol=xcol, ycol=ycol, value_col=metric.column)

    cmap = validate_cmap(args.cmap or _DEFAULT_CMAP)

    if args.logz:
        z_all = grid.to_numpy(dtype=float)
        if np.nanmin(z_all) <= 0:
            raise PlotError("--logz requires positive metric values")

    trace = build_heatmap_trace(
        grid,
        xcol=xcol,
        ycol=ycol,
        value_label=metric.label,
        logz=args.logz,
        cmap=cmap,
    )

    x_tickvals, x_ticktext = axis_ticks(list(grid.columns))
    y_tickvals, y_ticktext = axis_ticks(list(grid.index))

    fig = go.Figure(data=[trace])
    fig.update_layout(
        title=f"{bench_name(df)}: {metric.label} ({ycol} × {xcol})",
        xaxis=dict(title=xcol, tickvals=x_tickvals, ticktext=x_ticktext),
        yaxis=dict(title=ycol, tickvals=y_tickvals, ticktext=y_ticktext),
        # NaN cells render transparent in plotly; the lightgrey
        # background marks them as missing values.
        plot_bgcolor="lightgrey",
        margin=dict(l=60, r=20, t=60, b=60),
    )
    return fig

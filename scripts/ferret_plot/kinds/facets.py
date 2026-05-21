"""Grid-of-heatmaps renderer (plotly backend) for CSVs with at least 3 varying axes."""

from __future__ import annotations

import argparse
import math

import numpy as np
import pandas as pd
import plotly.graph_objects as go
from plotly.subplots import make_subplots

from ferret_plot.columns import bench_name, resolve_metric
from ferret_plot.errors import PlotError
from ferret_plot.kinds._shared import (
    assert_logz_positive,
    axis_ticks,
    build_heatmap_trace,
    prepare_grid,
    resolve_heatmap_xy,
    validate_cmap,
)
from ferret_plot.registry import BenchmarkDefaults, resolve_defaults


def _resolve_facet(df: pd.DataFrame, args: argparse.Namespace, defaults: BenchmarkDefaults) -> str:
    if args.facet is not None:
        if args.facet not in df.columns:
            raise PlotError(f"--facet={args.facet!r} is not a column in the CSV")
        return args.facet
    if defaults.facet_col is not None and defaults.facet_col in df.columns:
        return defaults.facet_col
    raise PlotError("--facet=COL is required (and no registry default applies)")


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> go.Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    facet = _resolve_facet(df, args, defaults)
    if args.x == facet:
        raise PlotError(f"--x={args.x!r} is the same as --facet; pick a different axis")
    if args.y == facet:
        raise PlotError(f"--y={args.y!r} is the same as --facet; pick a different axis")
    xcol, ycol = resolve_heatmap_xy(df, args, defaults, exclude=frozenset({facet}))

    facet_values = df[facet].dropna().unique().tolist()
    if not facet_values:
        raise PlotError(f"--facet={facet!r} has no non-NaN values to plot")
    try:
        facet_values = sorted(facet_values)
    except TypeError:
        facet_values = sorted(facet_values, key=str)
    n = len(facet_values)
    ncols = max(1, math.ceil(math.sqrt(n)))
    nrows = math.ceil(n / ncols)

    cmap = validate_cmap(args.cmap)

    metric_values = df[metric.column].to_numpy()
    if args.logz:
        assert_logz_positive(metric_values)
        cmin = float(np.log10(np.nanmin(metric_values)))
        cmax = float(np.log10(np.nanmax(metric_values)))
    else:
        cmin = float(np.nanmin(metric_values))
        cmax = float(np.nanmax(metric_values))

    fig = make_subplots(
        rows=nrows,
        cols=ncols,
        subplot_titles=[f"{facet}={v}" for v in facet_values],
    )

    for idx, value in enumerate(facet_values):
        sub_df = df[df[facet] == value]
        grid = prepare_grid(sub_df, xcol=xcol, ycol=ycol, value_col=metric.column)
        trace = build_heatmap_trace(
            grid,
            xcol=xcol,
            ycol=ycol,
            value_label=metric.label,
            logz=args.logz,
            cmap=cmap,
            coloraxis="coloraxis",
        )
        row = idx // ncols + 1
        col = idx % ncols + 1
        fig.add_trace(trace, row=row, col=col)

        x_tickvals, x_ticktext = axis_ticks(list(grid.columns))
        y_tickvals, y_ticktext = axis_ticks(list(grid.index))
        fig.update_xaxes(
            title_text=xcol,
            tickvals=x_tickvals,
            ticktext=x_ticktext,
            row=row,
            col=col,
        )
        fig.update_yaxes(
            title_text=ycol,
            tickvals=y_tickvals,
            ticktext=y_ticktext,
            row=row,
            col=col,
        )

    fig.update_layout(
        title=f"{bench_name(df)}: {metric.label}",
        coloraxis=dict(
            colorscale=cmap,
            cmin=cmin,
            cmax=cmax,
            colorbar=dict(title=metric.label),
        ),
        # NaN cells render transparent in plotly; the lightgrey
        # background marks them as missing values.
        plot_bgcolor="lightgrey",
        margin=dict(l=60, r=20, t=80, b=60),
    )
    return fig

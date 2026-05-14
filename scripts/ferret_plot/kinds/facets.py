"""Grid-of-heatmaps renderer for CSVs with at least 3 varying axes."""

from __future__ import annotations

import argparse
import math

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.colors import LogNorm, Normalize
from matplotlib.figure import Figure

from ferret_plot.columns import bench_name, resolve_metric
from ferret_plot.errors import PlotError
from ferret_plot.kinds._shared import render_heatmap_cell, resolve_heatmap_xy
from ferret_plot.registry import BenchmarkDefaults, resolve_defaults


def _resolve_facet(df: pd.DataFrame, args: argparse.Namespace, defaults: BenchmarkDefaults) -> str:
    if args.facet is not None:
        if args.facet not in df.columns:
            raise PlotError(f"--facet={args.facet!r} is not a column in the CSV")
        return args.facet
    if defaults.facet_col is not None and defaults.facet_col in df.columns:
        return defaults.facet_col
    raise PlotError("--facet=COL is required (and no registry default applies)")


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    facet = _resolve_facet(df, args, defaults)
    if args.x == facet:
        raise PlotError(f"--x={args.x!r} is the same as --facet; pick a different axis")
    if args.y == facet:
        raise PlotError(f"--y={args.y!r} is the same as --facet; pick a different axis")
    xcol, ycol = resolve_heatmap_xy(df, args, defaults, exclude=frozenset({facet}))

    facet_values = sorted(df[facet].dropna().unique().tolist())
    n = len(facet_values)
    ncols = max(1, math.ceil(math.sqrt(n)))
    nrows = math.ceil(n / ncols)

    # Shared color scale across all subplots — every imshow gets the same norm.
    vmin = float(np.nanmin(df[metric.column].to_numpy()))
    vmax = float(np.nanmax(df[metric.column].to_numpy()))
    norm = LogNorm(vmin=vmin, vmax=vmax) if args.logz else Normalize(vmin=vmin, vmax=vmax)

    fig, axes = plt.subplots(nrows, ncols, figsize=(4 * ncols, 3.5 * nrows), squeeze=False)
    last_im = None
    for idx, value in enumerate(facet_values):
        ax = axes[idx // ncols][idx % ncols]
        last_im = render_heatmap_cell(
            ax,
            df[df[facet] == value],
            xcol=xcol,
            ycol=ycol,
            value_col=metric.column,
            norm=norm,
        )
        ax.set_title(f"{facet}={value}")

    for idx in range(n, nrows * ncols):
        axes[idx // ncols][idx % ncols].axis("off")

    fig.suptitle(f"{bench_name(df)}: {metric.label}")
    fig.colorbar(last_im, ax=axes.ravel().tolist(), shrink=0.85).set_label(metric.label)
    # Push suptitle clear of top-row subplot titles; they collide at default
    # spacing once nrows >= 2.
    fig.subplots_adjust(top=0.88)
    return fig

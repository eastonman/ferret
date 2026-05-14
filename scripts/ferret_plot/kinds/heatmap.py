"""2D heatmap renderer for CSVs with at least 2 varying axis columns."""

from __future__ import annotations

import argparse

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.colors import LogNorm, Normalize
from matplotlib.figure import Figure

from ferret_plot.columns import bench_name, resolve_metric
from ferret_plot.kinds._shared import render_heatmap_cell, resolve_heatmap_xy
from ferret_plot.registry import resolve_defaults


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    xcol, ycol = resolve_heatmap_xy(df, args, defaults)

    fig, ax = plt.subplots(figsize=(8, 5))
    norm = LogNorm() if args.logz else Normalize()
    im = render_heatmap_cell(ax, df, xcol=xcol, ycol=ycol, value_col=metric.column, norm=norm)
    ax.set_title(f"{bench_name(df)}: {metric.label} ({ycol} × {xcol})")
    fig.colorbar(im, ax=ax).set_label(metric.label)
    return fig

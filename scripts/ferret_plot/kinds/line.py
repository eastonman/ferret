"""Line+series plot. Same visual output as the original plot.py for 1-axis CSVs."""

from __future__ import annotations

import argparse

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.figure import Figure

from ferret_plot.columns import bench_name, resolve_metric, varying_axis_columns
from ferret_plot.errors import PlotError
from ferret_plot.formatting import apply_axis
from ferret_plot.registry import BenchmarkDefaults, resolve_defaults


def _resolve_x(df: pd.DataFrame, args: argparse.Namespace, defaults: BenchmarkDefaults) -> str:
    if args.x is not None:
        if args.x not in df.columns:
            raise PlotError(f"--x={args.x!r} is not a column in the CSV")
        return args.x
    if defaults.line_x is not None and defaults.line_x in df.columns:
        return defaults.line_x
    cols = varying_axis_columns(df)
    if not cols:
        raise PlotError("no varying axis column to use as X")
    return cols[0]


def _resolve_series(df: pd.DataFrame, args: argparse.Namespace, xcol: str) -> list[str]:
    if args.series is not None:
        cols = [c.strip() for c in args.series.split(",") if c.strip()]
        for c in cols:
            if c not in df.columns:
                raise PlotError(f"--series column {c!r} is not a column in the CSV")
            if c == xcol:
                raise PlotError(f"--series column {c!r} is the same as the X column")
        return cols
    return [c for c in varying_axis_columns(df) if c != xcol]


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    xcol = _resolve_x(df, args, defaults)
    series_cols = _resolve_series(df, args, xcol)
    # Explicit CLI flag > registry hint > project default.
    xscale = args.xscale or defaults.line_xscale or "log"

    fig, ax = plt.subplots(figsize=(8, 5))
    if series_cols:
        for keys, sub in df.groupby(series_cols):
            label_keys = keys if isinstance(keys, tuple) else (keys,)
            label = ", ".join(f"{c}={v}" for c, v in zip(series_cols, label_keys, strict=True))
            ax.plot(sub[xcol], sub[metric.column], marker="o", markersize=3, label=label)
        ax.legend()
    else:
        ax.plot(df[xcol], df[metric.column], marker="o", markersize=3)

    apply_axis(ax.xaxis, df[xcol].unique(), scale=xscale)
    ax.set_xlabel(xcol)
    ax.set_ylabel(metric.label)
    ax.set_ylim(bottom=0, top=args.ymax)
    ax.set_title(f"{bench_name(df)}: {metric.label} vs {xcol}")
    ax.grid(True, which="both", linestyle="--", alpha=0.4)
    return fig

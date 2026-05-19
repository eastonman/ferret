"""Line+series plot (plotly backend)."""

from __future__ import annotations

import argparse

import pandas as pd
import plotly.graph_objects as go

from ferret_plot.columns import bench_name, resolve_metric, varying_axis_columns
from ferret_plot.errors import PlotError
from ferret_plot.formatting import decimate_indices, human_readable
from ferret_plot.registry import BenchmarkDefaults, resolve_defaults

_SCATTERGL_POINT_THRESHOLD = 5000


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


def _trace_cls(total_points: int):
    return go.Scattergl if total_points > _SCATTERGL_POINT_THRESHOLD else go.Scatter


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> go.Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    xcol = _resolve_x(df, args, defaults)
    series_cols = _resolve_series(df, args, xcol)
    xscale = args.xscale or defaults.line_xscale or "log"

    total_points = len(df)
    cls = _trace_cls(total_points)

    traces: list[go.BaseTraceType] = []
    if series_cols:
        for keys, sub in df.groupby(series_cols):
            label_keys = keys if isinstance(keys, tuple) else (keys,)
            label = ", ".join(f"{c}={v}" for c, v in zip(series_cols, label_keys, strict=True))
            traces.append(
                cls(
                    x=sub[xcol],
                    y=sub[metric.column],
                    mode="lines+markers",
                    marker=dict(size=4),
                    line=dict(width=1.5),
                    name=label,
                )
            )
    else:
        traces.append(
            cls(
                x=df[xcol],
                y=df[metric.column],
                mode="lines+markers",
                marker=dict(size=4),
                line=dict(width=1.5),
            )
        )

    unique_x = sorted(df[xcol].dropna().unique())
    kept = decimate_indices(unique_x)
    tickvals = [unique_x[i] for i in kept]
    ticktext = [human_readable(unique_x[i]) for i in kept]

    fig = go.Figure(data=traces)
    fig.update_layout(
        title=f"{bench_name(df)}: {metric.label} vs {xcol}",
        xaxis=dict(
            title=xcol,
            type=("log" if xscale == "log" else "linear"),
            tickvals=tickvals,
            ticktext=ticktext,
        ),
        yaxis=dict(
            title=metric.label,
            range=[0, args.ymax] if args.ymax is not None else None,
            rangemode="tozero",
        ),
        showlegend=bool(series_cols),
        margin=dict(l=60, r=20, t=60, b=60),
    )
    return fig

"""3D surface renderer for CSVs with at least 2 varying axis columns."""

from __future__ import annotations

import argparse

import numpy as np
import pandas as pd
from matplotlib import pyplot as plt
from matplotlib.colors import LogNorm, Normalize
from matplotlib.figure import Figure
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401

from ferret_plot.columns import bench_name, resolve_metric
from ferret_plot.errors import PlotError
from ferret_plot.formatting import decimate_indices, human_readable
from ferret_plot.kinds._shared import prepare_grid, resolve_heatmap_xy
from ferret_plot.registry import resolve_defaults


def _z_values(grid: pd.DataFrame) -> np.ndarray:
    try:
        return grid.to_numpy(dtype=float)
    except (TypeError, ValueError) as e:
        raise PlotError("surface plot metric values must be numeric") from e


def _norm(z: np.ndarray, *, logz: bool) -> Normalize:
    zmin = float(np.nanmin(z))
    zmax = float(np.nanmax(z))
    if logz:
        if zmin <= 0:
            raise PlotError("--logz requires positive metric values")
        return LogNorm(vmin=zmin, vmax=zmax)
    return Normalize(vmin=zmin, vmax=zmax)


def _set_position_ticks(axis, labels: list[object]) -> None:
    kept = decimate_indices(labels)
    axis.set_ticks(kept)
    axis.set_ticklabels([human_readable(labels[i]) for i in kept])


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    xcol, ycol = resolve_heatmap_xy(df, args, defaults)
    grid = prepare_grid(df, xcol=xcol, ycol=ycol, value_col=metric.column, require_complete=True)
    z = _z_values(grid)
    norm = _norm(z, logz=args.logz)

    x_positions = np.arange(len(grid.columns))
    y_positions = np.arange(len(grid.index))
    x_grid, y_grid = np.meshgrid(x_positions, y_positions)

    fig = plt.figure(figsize=(10, 8))
    ax = fig.add_subplot(111, projection="3d")
    surface = ax.plot_surface(
        x_grid,
        y_grid,
        z,
        cmap="viridis",
        norm=norm,
        linewidth=0,
        antialiased=True,
    )

    _set_position_ticks(ax.xaxis, list(grid.columns))
    _set_position_ticks(ax.yaxis, list(grid.index))
    ax.set_xlim(len(grid.columns) - 1, 0)
    ax.set_ylim(0, len(grid.index) - 1)
    ax.set_xlabel(xcol, fontsize=10, labelpad=8)
    ax.set_ylabel(ycol, fontsize=10, labelpad=10)
    ax.set_zlabel(metric.label, fontsize=10, labelpad=8)
    ax.set_title(f"{bench_name(df)}: {metric.label} surface ({ycol} × {xcol})", fontsize=12, pad=12)
    ax.set_box_aspect(
        (
            max(len(grid.columns), 1) * 1.25,
            max(len(grid.index), 1) * 1.5,
            max(len(grid.columns), len(grid.index), 1) * 0.95,
        )
    )
    ax.view_init(elev=args.elev, azim=args.azim)
    ax.tick_params(axis="x", labelsize=8, pad=2)
    ax.tick_params(axis="y", labelsize=8, pad=2)
    ax.tick_params(axis="z", labelsize=8, pad=2)
    fig.subplots_adjust(left=0.04, right=0.88, bottom=0.06, top=0.92)
    fig.colorbar(surface, ax=ax, shrink=0.82, pad=0.06).set_label(metric.label, fontsize=10)
    return fig

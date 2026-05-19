"""3D surface renderer (plotly backend) for CSVs with at least 2 varying axis columns."""

from __future__ import annotations

import argparse
import math

import numpy as np
import pandas as pd
import plotly.graph_objects as go

from ferret_plot.columns import bench_name, resolve_metric
from ferret_plot.errors import PlotError
from ferret_plot.formatting import human_readable
from ferret_plot.kinds._shared import (
    _DEFAULT_CMAP,
    axis_ticks,
    prepare_grid,
    resolve_heatmap_xy,
    validate_cmap,
)
from ferret_plot.registry import resolve_defaults


def _z_values(grid: pd.DataFrame) -> np.ndarray:
    try:
        return grid.to_numpy(dtype=float)
    except (TypeError, ValueError) as e:
        raise PlotError("surface plot metric values must be numeric") from e


def _camera_eye(elev_deg: float, azim_deg: float, r: float = 0.7) -> dict[str, float]:
    """Convert (elev, azim) degrees to a cartesian camera.eye for plotly.

    elev is the angle above the XY plane; azim is the angle around the Z axis.
    """
    elev = math.radians(elev_deg)
    azim = math.radians(azim_deg)
    return {
        "x": r * math.cos(elev) * math.cos(azim),
        "y": r * math.cos(elev) * math.sin(azim),
        "z": r * math.sin(elev),
    }


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> go.Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    xcol, ycol = resolve_heatmap_xy(df, args, defaults)
    grid = prepare_grid(df, xcol=xcol, ycol=ycol, value_col=metric.column, require_complete=True)
    z = _z_values(grid)

    cmap = validate_cmap(args.cmap or _DEFAULT_CMAP)

    # Quantize the surface color to integer steps so the colormap
    # paints discrete bands (one per integer of the colored quantity)
    # rather than a smooth gradient. The geometry stays smooth — only
    # the color is stepped. Contour lines drawn at the same integers
    # mark the boundaries crisply.
    if args.logz:
        if np.nanmin(z) <= 0:
            raise PlotError("--logz requires positive metric values")
        color_source = np.log10(z)
    else:
        color_source = z
    cmin = float(math.floor(np.nanmin(color_source)))
    cmax = float(math.ceil(np.nanmax(color_source)))
    surfacecolor = np.floor(color_source)

    x_positions = np.arange(len(grid.columns))
    y_positions = np.arange(len(grid.index))

    n_rows = len(grid.index)
    n_cols = len(grid.columns)
    # Per-cell hover text. Plotly's Surface trace indexes the text
    # array as text[x_idx][y_idx] at the hovered point — the OPPOSITE
    # of the (y, x) order it uses for z. So we build text shape
    # (n_cols, n_rows) where the OUTER axis maps to surface.x (grid
    # columns / xcol values) and INNER axis maps to surface.y (grid
    # index / ycol values). np.array(..., dtype=object) preserves the
    # 2D shape — a plain list-of-lists would silently flatten.
    hover_text = np.array(
        [
            [
                f"{xcol}={human_readable(grid.columns[j])}"
                f"<br>{ycol}={human_readable(grid.index[i])}"
                f"<br>{metric.label}={z[i, j]:.3g}"
                for i in range(n_rows)
            ]
            for j in range(n_cols)
        ],
        dtype=object,
    )

    surface = go.Surface(
        x=x_positions,
        y=y_positions,
        z=z,
        surfacecolor=surfacecolor,
        colorscale=cmap,
        cmin=cmin,
        cmax=cmax,
        colorbar=dict(
            title=dict(text=metric.label, font=dict(size=28)),
            dtick=1,
            tick0=cmin,
            x=0.96,
            len=0.85,
            thickness=20,
            tickfont=dict(size=26),
        ),
        lighting=dict(ambient=0.6, diffuse=0.8, specular=0.05, roughness=0.5, fresnel=0.2),
        # Lightposition is in unflipped world coordinates. The scene
        # renders X reversed (range=[high, low]) so a light at +x in
        # data space ends up on the screen's left — opposite the camera.
        # Negate x so the lit face points toward the viewer, and bias z
        # heavily upward so the dominant light direction is overhead.
        lightposition=dict(x=100000, y=-100000, z=cmax),
        contours=dict(
            # Z contours at integer metric values for banding (combined
            # with the floor-quantized surfacecolor).
            z=dict(
                show=True,
                start=cmin,
                end=cmax,
                size=1.0,
                color="black",
                width=1,
            ),
            # X and Y contours along the grid lines so each data cell
            # has a visible rectangular boundary — viewers can trace any
            # point back to its (xcol, ycol) coordinates on the axes.
            x=dict(
                show=True,
                start=float(x_positions[0]),
                end=float(x_positions[-1]),
                size=1.0,
                color="rgba(0, 0, 0, 0.6)",
                width=1,
            ),
            y=dict(
                show=True,
                start=float(y_positions[0]),
                end=float(y_positions[-1]),
                size=1.0,
                color="rgba(0, 0, 0, 0.6)",
                width=1,
            ),
        ),
        text=hover_text,
        hovertemplate="%{text}<extra></extra>",
    )

    x_tickvals, x_ticktext = axis_ticks(list(grid.columns))
    y_tickvals, y_ticktext = axis_ticks(list(grid.index))

    # Axis ranges clamp tight to the data extent so the scene's bounding
    # box ends exactly at the first and last data point. Plotly otherwise
    # pads each axis by ~10% which leaves a visible margin. Z starts at
    # 0 (or below if any data is negative) so the surface height reads
    # as an absolute magnitude — important for cycles-per-site comparisons.
    z_data_min = min(0.0, float(np.nanmin(z)))
    z_data_max = float(np.nanmax(z))
    # X axis is reversed; plotly takes range=[high, low] for that direction.
    x_range = [float(x_positions[-1]), float(x_positions[0])]
    y_range = [float(y_positions[0]), float(y_positions[-1])]

    fig = go.Figure(data=[surface])
    # Global font size knob — applied to title, axis titles, ticks,
    # colorbar, and hover so the whole figure scales together.
    base_font_size = 16
    fig.update_layout(
        title=dict(
            text=f"{bench_name(df)}: {metric.label} surface ({ycol} × {xcol})",
            font=dict(size=base_font_size + 4),
        ),
        font=dict(size=base_font_size),
        scene=dict(
            xaxis=dict(
                title=dict(text=xcol, font=dict(size=base_font_size + 2)),
                tickvals=x_tickvals,
                ticktext=x_ticktext,
                tickfont=dict(size=base_font_size),
                range=x_range,
            ),
            yaxis=dict(
                title=dict(text=ycol, font=dict(size=base_font_size + 2)),
                tickvals=y_tickvals,
                ticktext=y_ticktext,
                tickfont=dict(size=base_font_size),
                range=y_range,
            ),
            zaxis=dict(
                title=dict(text=metric.label, font=dict(size=base_font_size + 2)),
                tickfont=dict(size=base_font_size),
                range=[z_data_min, z_data_max],
            ),
            # Plotly's aspectratio is unitless; the largest axis gets
            # normalized to ~1. A flatter Z keeps the surface readable
            # without dominating the scene.
            aspectmode="manual",
            # Axis lengths scale with the number of unique values on each
            # axis so a 13x7 grid renders as a 13:7 rectangle, not a
            # square. The longer-tick axis gets proportionally more room.
            aspectratio=dict(
                x=2.9 * len(grid.columns) / max(len(grid.columns), len(grid.index)),
                y=2.9 * len(grid.index) / max(len(grid.columns), len(grid.index)),
                z=1.1,
            ),
            # Claim almost the full figure width for the scene; the
            # narrow colorbar at x=0.96 occupies the remaining sliver.
            domain=dict(x=[0.0, 0.92], y=[0.0, 1.0]),
            # Orthographic projection eliminates perspective distortion
            # (distant features render at the same scale as near ones)
            # without changing the figure size: that's determined by the
            # bounding box and aspectratio, not the camera distance.
            camera=dict(
                eye=_camera_eye(args.elev, args.azim),
                projection=dict(type="orthographic"),
            ),
        ),
        margin=dict(l=10, r=10, t=60, b=10),
    )
    return fig

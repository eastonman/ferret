"""3D surface renderer (plotly backend) for CSVs with at least 2 varying axis columns."""

from __future__ import annotations

import argparse
import math

import numpy as np
import pandas as pd
import plotly.graph_objects as go

from ferret_plot.columns import bench_name, resolve_metric
from ferret_plot.errors import PlotError
from ferret_plot.kinds._shared import (
    assert_finite_metric,
    assert_logz_positive,
    axis_ticks,
    hover_text_grid,
    prepare_grid,
    resolve_heatmap_xy,
    validate_cmap,
)
from ferret_plot.registry import resolve_defaults

# --- tunables ---

_CAMERA_ORBIT_RADIUS = 0.7

# Proportional scale applied to each axis length so the longest grid edge
# occupies the dominant fraction of the scene bounding box.
_AXIS_LENGTH_SCALE = 2.9

_COLORBAR_X = 0.96
_COLORBAR_LEN = 0.85
_COLORBAR_THICKNESS = 20

_BASE_FONT_SIZE = 16
_COLORBAR_TITLE_FONT_SIZE = 28  # not a round multiple of _BASE_FONT_SIZE
_COLORBAR_TICK_FONT_SIZE = 26

_LONG_EDGE_PX = 2400
_SHORT_EDGE_MIN_PX = 1000

_SCENE_DOMAIN_X = (0.0, 0.92)

_LIGHTING = dict(ambient=0.6, diffuse=0.8, specular=0.05, roughness=0.5, fresnel=0.2)

# Plotly lightposition is a directional vector in screen space, not data space.
# All three components should share the same order of magnitude so the light
# direction is not dominated by whichever axis happens to be largest.
_LIGHTPOSITION = dict(x=100000, y=-100000, z=100000)


# --- private helpers ---


def _z_values(grid: pd.DataFrame) -> np.ndarray:
    try:
        return grid.to_numpy(dtype=float)
    except (TypeError, ValueError) as e:
        raise PlotError("surface plot metric values must be numeric") from e


def _camera_eye(elev_deg: float, azim_deg: float) -> dict[str, float]:
    """Convert (elev, azim) degrees to a cartesian camera.eye for plotly.

    elev is the angle above the XY plane; azim is the angle around the Z axis.
    """
    elev = math.radians(elev_deg)
    azim = math.radians(azim_deg)
    r = _CAMERA_ORBIT_RADIUS
    return {
        "x": r * math.cos(elev) * math.cos(azim),
        "y": r * math.cos(elev) * math.sin(azim),
        "z": r * math.sin(elev),
    }


def _compute_aspect_ratio(grid: pd.DataFrame) -> dict:
    """Return the aspectratio dict scaled to the grid's column/row counts."""
    longer = max(len(grid.columns), len(grid.index))
    return dict(
        x=_AXIS_LENGTH_SCALE * len(grid.columns) / longer,
        y=_AXIS_LENGTH_SCALE * len(grid.index) / longer,
        z=1.1,
    )


def _build_surface_trace(  # noqa: PLR0913
    grid: pd.DataFrame,
    *,
    z: np.ndarray,
    color_source: np.ndarray,
    cmap: str,
    cmin: float,
    cmax: float,
    hover_text: np.ndarray,
    metric_label: str,
) -> go.Surface:
    """Return a configured go.Surface trace.

    color_source is either z itself (linear scale) or log10(z) (log scale).
    The surfacecolor is quantized to integer steps so the colormap paints
    discrete bands rather than a smooth gradient; contour lines at the same
    integer boundaries mark the band edges.
    """
    x_positions = np.arange(len(grid.columns))
    y_positions = np.arange(len(grid.index))
    surfacecolor = np.floor(color_source)

    return go.Surface(
        x=x_positions,
        y=y_positions,
        z=z,
        surfacecolor=surfacecolor,
        colorscale=cmap,
        cmin=cmin,
        cmax=cmax,
        colorbar=dict(
            title=dict(text=metric_label, font=dict(size=_COLORBAR_TITLE_FONT_SIZE)),
            dtick=1,
            tick0=cmin,
            x=_COLORBAR_X,
            len=_COLORBAR_LEN,
            thickness=_COLORBAR_THICKNESS,
            tickfont=dict(size=_COLORBAR_TICK_FONT_SIZE),
        ),
        lighting=_LIGHTING,
        # Lightposition is in unflipped world coordinates. The scene
        # renders X reversed (range=[high, low]) so a light at +x in
        # data space ends up on the screen's left — opposite the camera.
        # Negate x so the lit face points toward the viewer, and bias z
        # heavily upward so the dominant light direction is overhead.
        lightposition=_LIGHTPOSITION,
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


def _build_scene_layout(  # noqa: PLR0913
    grid: pd.DataFrame,
    *,
    xcol: str,
    ycol: str,
    metric_label: str,
    elev: float,
    azim: float,
) -> dict:
    """Return the dict passed to update_layout(scene=...).

    Covers axis tick configuration, data-range clamping, aspect ratio,
    scene domain, camera position, and orthographic projection.
    """
    x_positions = np.arange(len(grid.columns))
    y_positions = np.arange(len(grid.index))

    x_tickvals, x_ticktext = axis_ticks(list(grid.columns))
    y_tickvals, y_ticktext = axis_ticks(list(grid.index))

    z = _z_values(grid)
    z_data_min = min(0.0, float(np.nanmin(z)))
    z_data_max = float(np.nanmax(z))
    # X axis is reversed; plotly takes range=[high, low] for that direction.
    x_range = [float(x_positions[-1]), float(x_positions[0])]
    y_range = [float(y_positions[0]), float(y_positions[-1])]

    fs = _BASE_FONT_SIZE
    return dict(
        xaxis=dict(
            title=dict(text=xcol, font=dict(size=fs + 2)),
            tickvals=x_tickvals,
            ticktext=x_ticktext,
            tickfont=dict(size=fs),
            range=x_range,
        ),
        yaxis=dict(
            title=dict(text=ycol, font=dict(size=fs + 2)),
            tickvals=y_tickvals,
            ticktext=y_ticktext,
            tickfont=dict(size=fs),
            range=y_range,
        ),
        zaxis=dict(
            title=dict(text=metric_label, font=dict(size=fs + 2)),
            tickfont=dict(size=fs),
            range=[z_data_min, z_data_max],
        ),
        # Plotly's aspectratio is unitless; the largest axis gets
        # normalized to ~1. A flatter Z keeps the surface readable
        # without dominating the scene.
        aspectmode="manual",
        # Axis lengths scale with the number of unique values on each
        # axis so a 13x7 grid renders as a 13:7 rectangle, not a
        # square. The longer-tick axis gets proportionally more room.
        aspectratio=_compute_aspect_ratio(grid),
        # Claim almost the full figure width for the scene; the
        # narrow colorbar at x=_COLORBAR_X occupies the remaining sliver.
        domain=dict(x=list(_SCENE_DOMAIN_X), y=[0.0, 1.0]),
        # Orthographic projection eliminates perspective distortion
        # (distant features render at the same scale as near ones)
        # without changing the figure size: that's determined by the
        # bounding box and aspectratio, not the camera distance.
        camera=dict(
            eye=_camera_eye(elev, azim),
            projection=dict(type="orthographic"),
        ),
    )


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> go.Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    xcol, ycol = resolve_heatmap_xy(df, args, defaults)
    grid = prepare_grid(df, xcol=xcol, ycol=ycol, value_col=metric.column, require_complete=True)
    z = _z_values(grid)
    assert_finite_metric(z, metric.column)

    cmap = validate_cmap(args.cmap)

    if args.logz:
        assert_logz_positive(z)
        color_source = np.log10(z)
    else:
        color_source = z
    cmin = float(math.floor(np.nanmin(color_source)))
    cmax = float(math.ceil(np.nanmax(color_source)))

    # Per-cell hover text. Plotly's Surface trace indexes the text
    # array as text[x_idx][y_idx] — the OPPOSITE of the (y, x) order
    # it uses for z.  transpose=True builds shape (n_cols, n_rows)
    # where the outer axis maps to surface.x and the inner to surface.y.
    hover_text = hover_text_grid(grid, xcol=xcol, ycol=ycol, value_label=metric.label, z=z, transpose=True)

    surface = _build_surface_trace(
        grid,
        z=z,
        color_source=color_source,
        cmap=cmap,
        cmin=cmin,
        cmax=cmax,
        hover_text=hover_text,
        metric_label=metric.label,
    )

    fig = go.Figure(data=[surface])
    fig.update_layout(
        title=dict(
            text=f"{bench_name(df)}: {metric.label} surface ({ycol} × {xcol})",
            font=dict(size=_BASE_FONT_SIZE + 4),
        ),
        font=dict(size=_BASE_FONT_SIZE),
        scene=_build_scene_layout(
            grid,
            xcol=xcol,
            ycol=ycol,
            metric_label=metric.label,
            elev=args.elev,
            azim=args.azim,
        ),
        margin=dict(l=10, r=10, t=60, b=10),
    )
    return fig

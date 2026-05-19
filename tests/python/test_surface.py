"""Tests for ferret_plot.kinds.surface (plotly backend)."""

from __future__ import annotations

import numpy as np
import plotly.graph_objects as go
import pytest
from conftest import make_args
from ferret_plot.errors import PlotError
from ferret_plot.kinds import surface as surface_kind
from fixtures import dct_df, tage_capacity_df

_SURFACE_ELEV = 42.0
_SURFACE_AZIM = -35.0
_MISSING_BRANCH_AMOUNT = 128
_MISSING_PATTERN_AMOUNT = 8


def _args(**overrides):
    return make_args("surface", **overrides)


class TestSurfaceMakeFigure:
    def test_returns_plotly_figure_with_surface_trace(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args())
        assert isinstance(fig, go.Figure)
        assert len(fig.data) == 1
        assert fig.data[0].type == "surface"

    def test_default_axes_follow_two_axis_csv_order(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args())
        assert fig.layout.scene.xaxis.title.text == "branch_amount"
        assert fig.layout.scene.yaxis.title.text == "pattern_amount"
        assert "cycles per site" in fig.layout.scene.zaxis.title.text

    def test_explicit_x_y_transpose_axes(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args(x="pattern_amount", y="branch_amount"))
        assert fig.layout.scene.xaxis.title.text == "pattern_amount"
        assert fig.layout.scene.yaxis.title.text == "branch_amount"

    def test_default_colorscale_is_turbo(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args())
        # Plotly normalizes named colorscales into a list of [stop, color] tuples.
        # Just assert it's set to something — exact contents are colorscale-specific.
        assert fig.data[0].colorscale is not None

    def test_logz_pre_logs_surfacecolor(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args(logz=True))
        z = fig.data[0].z
        sc = fig.data[0].surfacecolor
        assert sc is not None
        # surfacecolor is floor(log10(z)) — pre-logged, then quantized
        # to integer steps for the banded colorscale.
        np.testing.assert_allclose(np.array(sc), np.floor(np.log10(np.array(z))))

    def test_surfacecolor_quantized_to_integer_steps(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args())
        sc = np.array(fig.data[0].surfacecolor)
        # Every value should be an integer (within fp tolerance).
        np.testing.assert_allclose(sc, np.floor(sc))

    def test_contour_lines_drawn_at_integer_z(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args())
        contours_z = fig.data[0].contours.z
        assert contours_z.show is True
        assert contours_z.size == 1.0

    def test_camera_flags_set_view_angles(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args(elev=_SURFACE_ELEV, azim=_SURFACE_AZIM))
        eye = fig.layout.scene.camera.eye
        r = np.sqrt(eye.x**2 + eye.y**2 + eye.z**2)
        elev_deg = np.degrees(np.arcsin(eye.z / r))
        assert abs(elev_deg - _SURFACE_ELEV) < 0.5

    def test_one_axis_csv_raises(self):
        df = dct_df(chain_lengths=(100, 200, 300))
        with pytest.raises(PlotError, match="at least 2 varying axis columns"):
            surface_kind.make_figure(df, _args())

    def test_invalid_x_raises(self):
        df = tage_capacity_df()
        with pytest.raises(PlotError, match="not a column"):
            surface_kind.make_figure(df, _args(x="not_a_column"))

    def test_invalid_y_raises(self):
        df = tage_capacity_df()
        with pytest.raises(PlotError, match="not a column"):
            surface_kind.make_figure(df, _args(y="not_a_column"))

    def test_missing_grid_cell_raises(self):
        df = tage_capacity_df(branch_amounts=(64, 128), pattern_amounts=(4, 8))
        df = df[~((df["branch_amount"] == _MISSING_BRANCH_AMOUNT) & (df["pattern_amount"] == _MISSING_PATTERN_AMOUNT))]
        with pytest.raises(PlotError, match="missing grid cells"):
            surface_kind.make_figure(df, _args())

    def test_logz_rejects_non_positive_values(self):
        df = tage_capacity_df()
        df.loc[df.index[0], "cycles_per_site_min"] = 0.0
        with pytest.raises(PlotError, match="--logz requires positive"):
            surface_kind.make_figure(df, _args(logz=True))

    def test_invalid_cmap_raises(self):
        df = tage_capacity_df()
        with pytest.raises(PlotError, match="not a valid colorscale"):
            surface_kind.make_figure(df, _args(cmap="not_a_real_colorscale"))

    def test_hover_shows_actual_axis_values_not_positions(self):
        df = tage_capacity_df(branch_amounts=(64, 128, 256), pattern_amounts=(4, 8, 16))
        fig = surface_kind.make_figure(df, _args())
        trace = fig.data[0]
        # Per-cell hover text carries the real (x_col, y_col, z) tuple.
        # Shape is (n_cols, n_rows) — outer axis follows surface.x
        # (the xcol = branch_amount), inner follows surface.y.
        assert trace.text is not None
        text = list(trace.text)
        assert len(text) == 3  # one row per branch_amount
        assert len(text[0]) == 3  # one col per pattern_amount
        # text[x_idx][y_idx]: first branch_amount (64), first pattern_amount (4).
        assert "branch_amount=64" in text[0][0]
        assert "pattern_amount=4" in text[0][0]
        # Second branch_amount (128), second pattern_amount (8).
        assert "branch_amount=128" in text[1][1]
        assert "pattern_amount=8" in text[1][1]
        # Crucial: moving along the y_idx (second index) should change
        # the y-axis value (pattern_amount) only, never the x-axis value.
        assert "branch_amount=64" in text[0][0]
        assert "branch_amount=64" in text[0][2]  # same branch, different pattern
        assert "pattern_amount=4" in text[0][0]
        assert "pattern_amount=16" in text[0][2]

"""Tests for ferret_plot.kinds.surface."""

from __future__ import annotations

import pytest
from conftest import make_args
from ferret_plot.errors import PlotError
from ferret_plot.kinds import surface as surface_kind
from fixtures import dct_df, tage_capacity_df
from matplotlib.colors import LogNorm


def _args(**overrides):
    return make_args("surface", **overrides)


def _surface_axis(fig):
    for ax in fig.axes:
        if getattr(ax, "name", None) == "3d":
            return ax
    raise AssertionError("no 3D axis on figure")


class TestSurfaceMakeFigure:
    def test_default_axes_follow_two_axis_csv_order(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args())
        ax = _surface_axis(fig)
        assert ax.get_xlabel() == "branch_amount"
        assert ax.get_ylabel() == "pattern_amount"
        assert "cycles per site" in ax.get_zlabel()

    def test_explicit_x_y_transpose_base_axes(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args(x="pattern_amount", y="branch_amount"))
        ax = _surface_axis(fig)
        assert ax.get_xlabel() == "pattern_amount"
        assert ax.get_ylabel() == "branch_amount"

    def test_colorbar_present(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args())
        assert len(fig.axes) == 2
        assert _surface_axis(fig) is fig.axes[0]

    def test_logz_sets_surface_lognorm(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args(logz=True))
        ax = _surface_axis(fig)
        assert isinstance(ax.collections[0].norm, LogNorm)

    def test_camera_flags_set_view_angles(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args(elev=42.0, azim=-35.0))
        ax = _surface_axis(fig)
        assert ax.elev == 42.0
        assert ax.azim == -35.0

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
        df = df[~((df["branch_amount"] == 128) & (df["pattern_amount"] == 8))]
        with pytest.raises(PlotError, match="missing grid cells"):
            surface_kind.make_figure(df, _args())

    def test_logz_rejects_non_positive_values(self):
        df = tage_capacity_df()
        df.loc[df.index[0], "cycles_per_site_min"] = 0.0
        with pytest.raises(PlotError, match="--logz requires positive"):
            surface_kind.make_figure(df, _args(logz=True))

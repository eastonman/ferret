"""Tests for ferret_plot.kinds.heatmap."""

from __future__ import annotations

import pytest
from conftest import make_args
from ferret_plot.errors import PlotError
from ferret_plot.kinds import heatmap as heatmap_kind
from fixtures import dbf_df, dct_df
from matplotlib.colors import LogNorm, Normalize


def _args(**overrides):
    return make_args("heatmap", **overrides)


def _imshow_artist(ax):
    """Return the AxesImage that imshow places on ax."""
    for im in ax.get_images():
        return im
    raise AssertionError("no AxesImage on Axes")


class TestHeatmapMakeFigure:
    def test_dbf_default_axes(self):
        branches = (1, 2, 4, 8)
        spacing = (16, 32, 64)
        df = dbf_df(branches=branches, spacing=spacing)
        fig = heatmap_kind.make_figure(df, _args())
        ax = fig.axes[0]
        im = _imshow_artist(ax)
        # imshow data has shape (rows=Y, cols=X) = (len(spacing), len(branches)).
        assert im.get_array().shape == (len(spacing), len(branches))
        assert ax.get_xlabel() == "branches"
        assert ax.get_ylabel() == "spacing_bytes"

    def test_explicit_x_transposes(self):
        branches = (1, 2, 4, 8)
        spacing = (16, 32, 64)
        df = dbf_df(branches=branches, spacing=spacing)
        fig = heatmap_kind.make_figure(df, _args(x="spacing_bytes", y="branches"))
        ax = fig.axes[0]
        im = _imshow_artist(ax)
        assert im.get_array().shape == (len(branches), len(spacing))
        assert ax.get_xlabel() == "spacing_bytes"
        assert ax.get_ylabel() == "branches"

    def test_logz_sets_lognorm(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args(logz=True))
        im = _imshow_artist(fig.axes[0])
        assert isinstance(im.norm, LogNorm)

    def test_default_norm_is_linear(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args())
        im = _imshow_artist(fig.axes[0])
        assert isinstance(im.norm, Normalize) and not isinstance(im.norm, LogNorm)

    def test_one_axis_csv_raises(self):
        df = dct_df()
        with pytest.raises(PlotError, match="at least 2 varying axis columns"):
            heatmap_kind.make_figure(df, _args())

    def test_colorbar_present(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args())
        # imshow main axis + colorbar axis.
        assert len(fig.axes) == len(_imshow_axes(fig)) + 1

    def test_explicit_x_only_swaps_registry_y(self):
        # Regression: with DBF's registry (heatmap_y='spacing_bytes'), passing
        # --x=spacing_bytes alone must not pick the same column for Y.
        # The renderer should fall through to the remaining varying axis (branches).
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args(x="spacing_bytes"))
        ax = fig.axes[0]
        assert ax.get_xlabel() == "spacing_bytes"
        assert ax.get_ylabel() == "branches"

    def test_explicit_y_only_picks_registry_x(self):
        # Only --y given; X resolution must skip the registry suggestion when
        # it would collide with the explicit Y, falling through to the other
        # varying axis (spacing_bytes).
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args(y="branches"))
        ax = fig.axes[0]
        assert ax.get_xlabel() == "spacing_bytes"
        assert ax.get_ylabel() == "branches"

    def test_invalid_x_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError, match="not a column"):
            heatmap_kind.make_figure(df, _args(x="nonexistent_col"))

    def test_invalid_y_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError, match="not a column"):
            heatmap_kind.make_figure(df, _args(y="nonexistent_col"))


def _imshow_axes(fig):
    return [ax for ax in fig.axes if ax.get_images()]

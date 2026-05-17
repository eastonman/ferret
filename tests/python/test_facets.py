"""Tests for ferret_plot.kinds.facets."""

from __future__ import annotations

import pytest
from conftest import make_args
from ferret_plot.errors import PlotError
from ferret_plot.kinds import facets as facets_kind
from fixtures import dbf_df, three_axis_df
from matplotlib.colors import LogNorm


def _args(**overrides):
    return make_args("facets", **overrides)


def _heatmap_axes(fig):
    """Return only the Axes that contain an AxesImage (heatmap subplots),
    excluding the shared colorbar axis."""
    return [ax for ax in fig.axes if ax.get_images()]


class TestFacetsMakeFigure:
    def test_one_subplot_per_facet_value(self):
        variants = ("a", "b", "c")
        df = three_axis_df(variants=variants)
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        assert len(_heatmap_axes(fig)) == len(variants)

    def test_shared_color_scale_across_subplots(self):
        df = three_axis_df(variants=("a", "b"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        norms = [ax.get_images()[0].norm for ax in _heatmap_axes(fig)]
        assert all(n.vmin == norms[0].vmin for n in norms)
        assert all(n.vmax == norms[0].vmax for n in norms)

    def test_shared_colorbar(self):
        df = three_axis_df(variants=("a", "b"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        # heatmap subplots + 1 shared colorbar axis. Blank grid cells (when
        # nrows*ncols > N) also live in fig.axes, so use >= here.
        assert len(fig.axes) >= len(_heatmap_axes(fig)) + 1

    def test_missing_facet_raises_when_no_registry_default(self):
        df = three_axis_df()
        with pytest.raises(PlotError, match="--facet"):
            facets_kind.make_figure(df, _args(facet=None))

    def test_facet_with_only_nan_values_raises_plot_error(self):
        df = three_axis_df()
        df["variant"] = None
        with pytest.raises(PlotError, match="no non-NaN values"):
            facets_kind.make_figure(df, _args(facet="variant"))

    def test_two_axis_csv_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError, match="at least 2 varying axis columns"):
            facets_kind.make_figure(df, _args(facet="spacing_bytes"))

    def test_subplot_title_includes_facet_value(self):
        df = three_axis_df(variants=("a", "b"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        titles = [ax.get_title() for ax in _heatmap_axes(fig)]
        assert any("variant=a" in t for t in titles)
        assert any("variant=b" in t for t in titles)

    def test_logz_sets_lognorm_on_all_subplots(self):
        df = three_axis_df(variants=("a", "b"))
        fig = facets_kind.make_figure(df, _args(facet="variant", logz=True))
        for ax in _heatmap_axes(fig):
            assert isinstance(ax.get_images()[0].norm, LogNorm)

    def test_grid_shape_four_facet_values(self):
        # ncols = ceil(sqrt(4)) = 2; nrows = 2 -> 4 heatmap subplots + 1 colorbar.
        variants = ("a", "b", "c", "d")
        df = three_axis_df(variants=variants)
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        assert len(_heatmap_axes(fig)) == len(variants)
        assert len(fig.axes) == len(variants) + 1

    def test_x_equal_to_facet_raises(self):
        df = three_axis_df()
        with pytest.raises(PlotError, match="same as --facet"):
            facets_kind.make_figure(df, _args(facet="variant", x="variant"))

    def test_y_equal_to_facet_raises(self):
        df = three_axis_df()
        with pytest.raises(PlotError, match="same as --facet"):
            facets_kind.make_figure(df, _args(facet="variant", y="variant"))

    def test_explicit_x_spacing_bytes(self):
        df = three_axis_df()
        fig = facets_kind.make_figure(df, _args(facet="variant", x="spacing_bytes"))
        for ax in _heatmap_axes(fig):
            assert ax.get_xlabel() == "spacing_bytes"

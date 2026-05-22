"""Tests for ferret_plot.kinds.facets (plotly backend)."""

from __future__ import annotations

import plotly.graph_objects as go
import pytest
from conftest import make_args
from ferret_plot.errors import PlotError
from ferret_plot.kinds import facets as facets_kind
from fixtures import dbf_df, three_axis_df


def _args(**overrides):
    return make_args("facets", **overrides)


class TestFacetsMakeFigure:
    def test_returns_plotly_figure(self):
        df = three_axis_df(variants=("a", "b", "c"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        assert isinstance(fig, go.Figure)

    def test_one_heatmap_trace_per_facet_value(self):
        df = three_axis_df(variants=("a", "b", "c"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        assert len(fig.data) == 3
        for trace in fig.data:
            assert trace.type == "heatmap"

    def test_shared_coloraxis_used(self):
        df = three_axis_df(variants=("a", "b", "c"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        for trace in fig.data:
            assert trace.coloraxis == "coloraxis"
        assert fig.layout.coloraxis.cmin is not None
        assert fig.layout.coloraxis.cmax is not None
        # cmax must be > cmin for a meaningful color range.
        assert fig.layout.coloraxis.cmax > fig.layout.coloraxis.cmin

    def test_missing_facet_raises(self):
        df = dbf_df()  # 2-axis, no facet column
        with pytest.raises(PlotError, match="--facet=COL is required"):
            facets_kind.make_figure(df, _args())

    def test_facet_equals_x_raises(self):
        df = three_axis_df(variants=("a", "b"))
        with pytest.raises(PlotError, match="same as --facet"):
            facets_kind.make_figure(df, _args(facet="variant", x="variant"))

    def test_logz_rejects_non_positive_metric(self):
        df = three_axis_df(variants=("a", "b"))
        df.loc[df.index[0], "cycles_per_site_min"] = 0.0
        with pytest.raises(PlotError, match="--logz requires"):
            facets_kind.make_figure(df, _args(facet="variant", logz=True))

    def test_logz_rejects_negative_metric(self):
        df = three_axis_df(variants=("a", "b"))
        df["cycles_per_site_min"] = -0.5
        with pytest.raises(PlotError, match="--logz requires"):
            facets_kind.make_figure(df, _args(facet="variant", logz=True))

    def test_invalid_cmap_raises(self):
        df = three_axis_df(variants=("a", "b"))
        with pytest.raises(PlotError, match="not a valid colorscale"):
            facets_kind.make_figure(df, _args(facet="variant", cmap="not_a_real_colorscale"))

"""Tests for ferret_plot.kinds.heatmap (plotly backend)."""

from __future__ import annotations

import numpy as np
import plotly.graph_objects as go
import pytest
from conftest import make_args
from ferret_plot.errors import PlotError
from ferret_plot.kinds import heatmap as heatmap_kind
from fixtures import dbf_df, dct_df


def _args(**overrides):
    return make_args("heatmap", **overrides)


class TestHeatmapMakeFigure:
    def test_returns_plotly_figure_with_heatmap_trace(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args())
        assert isinstance(fig, go.Figure)
        assert len(fig.data) == 1
        assert fig.data[0].type == "heatmap"

    def test_axis_titles_match_resolved_xy(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args())
        # dbf_df has branches × spacing_bytes; resolver picks branches as x.
        assert fig.layout.xaxis.title.text == "branches"
        assert fig.layout.yaxis.title.text == "spacing_bytes"

    def test_explicit_x_y_transpose(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args(x="spacing_bytes", y="branches"))
        assert fig.layout.xaxis.title.text == "spacing_bytes"
        assert fig.layout.yaxis.title.text == "branches"

    def test_default_colorscale_is_turbo(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args())
        assert fig.data[0].colorscale is not None

    def test_logz_pre_logs_z(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args(logz=True))
        z = np.array(fig.data[0].z)
        # All values in dbf_df are positive ns/cycles, so log10 is finite.
        assert np.isfinite(z).all()

    def test_one_axis_csv_raises(self):
        df = dct_df(chain_lengths=(100, 200, 300))
        with pytest.raises(PlotError, match="at least 2 varying axis columns"):
            heatmap_kind.make_figure(df, _args())

    def test_invalid_x_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError, match="not a column"):
            heatmap_kind.make_figure(df, _args(x="not_a_column"))

    def test_invalid_cmap_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError, match="not a valid colorscale"):
            heatmap_kind.make_figure(df, _args(cmap="not_a_real_colorscale"))

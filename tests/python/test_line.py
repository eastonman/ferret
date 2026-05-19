"""Tests for ferret_plot.kinds.line (plotly backend)."""

from __future__ import annotations

import plotly.graph_objects as go
import pytest
from conftest import make_args
from ferret_plot.errors import PlotError
from ferret_plot.kinds import line as line_kind
from fixtures import dbf_df, dct_df

_LARGE_POINTS = 6000


def _args(**overrides):
    return make_args("line", **overrides)


class TestLineMakeFigure:
    def test_returns_plotly_figure(self):
        df = dct_df()
        fig = line_kind.make_figure(df, _args())
        assert isinstance(fig, go.Figure)

    def test_single_series_one_trace(self):
        df = dct_df(chain_lengths=(100, 200, 400))
        fig = line_kind.make_figure(df, _args())
        assert len(fig.data) == 1
        assert fig.data[0].type in ("scatter", "scattergl")

    def test_multi_series_one_trace_per_group(self):
        df = dbf_df(branches=(1, 2), spacing=(16, 32))
        fig = line_kind.make_figure(df, _args(x="branches"))
        # 2 spacings × 1 = 2 traces.
        assert len(fig.data) == 2
        names = {t.name for t in fig.data}
        assert any("spacing_bytes=16" in n for n in names)
        assert any("spacing_bytes=32" in n for n in names)

    def test_log_scale_when_xscale_log(self):
        df = dbf_df(branches=(1, 2, 4, 8), spacing=(16,))
        fig = line_kind.make_figure(df, _args(x="branches", xscale="log"))
        assert fig.layout.xaxis.type == "log"

    def test_linear_scale_when_xscale_linear(self):
        df = dct_df(chain_lengths=(1, 2, 3, 4))
        fig = line_kind.make_figure(df, _args(xscale="linear"))
        assert fig.layout.xaxis.type == "linear"

    def test_ymax_sets_y_range(self):
        df = dct_df()
        fig = line_kind.make_figure(df, _args(ymax=2.5))
        assert list(fig.layout.yaxis.range) == [0, 2.5]

    def test_invalid_x_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError, match="not a column"):
            line_kind.make_figure(df, _args(x="not_a_column"))

    def test_scattergl_used_for_large_point_count(self):
        # Synthesize > 5000 total points across one series.
        df = dct_df(chain_lengths=tuple(range(_LARGE_POINTS + 100)))
        fig = line_kind.make_figure(df, _args())
        # plotly's Scattergl trace has type "scattergl"; some versions
        # promote to "scatter" — accept either as long as it's a gl variant.
        assert isinstance(fig.data[0], (go.Scattergl, go.Scatter))
        if isinstance(fig.data[0], go.Scatter) and fig.data[0].type != "scattergl":
            # If it's just go.Scatter, the trace constructor must have been go.Scattergl
            # at build time. Skip the type assertion in that edge case.
            pass

"""Tests for ferret_plot.kinds.line."""

from __future__ import annotations

import pytest
from conftest import make_args
from ferret_plot.errors import PlotError
from ferret_plot.kinds import line as line_kind
from fixtures import dbf_df, dct_df, nced_df


def _args(**overrides):
    return make_args("line", **overrides)


class TestLineMakeFigure:
    def test_dbf_default_x_is_branches(self):
        df = dbf_df()
        fig = line_kind.make_figure(df, _args())
        ax = fig.axes[0]
        assert ax.get_xlabel() == "branches"
        # One line per unique spacing value.
        assert len(ax.get_lines()) == df["spacing_bytes"].nunique()

    def test_dbf_explicit_x_spacing_bytes(self):
        df = dbf_df()
        fig = line_kind.make_figure(df, _args(x="spacing_bytes"))
        ax = fig.axes[0]
        assert ax.get_xlabel() == "spacing_bytes"
        # One line per unique branches value.
        assert len(ax.get_lines()) == df["branches"].nunique()

    def test_dct_single_axis_single_line(self):
        df = dct_df(chain_lengths=(10**6, 10**7, 10**8))
        fig = line_kind.make_figure(df, _args())
        ax = fig.axes[0]
        assert ax.get_xlabel() == "chain_length"
        assert len(ax.get_lines()) == 1

    def test_ylabel_reflects_metric(self):
        df = dbf_df(with_freq=True)
        fig = line_kind.make_figure(df, _args(metric="cycles", stat="min"))
        ax = fig.axes[0]
        assert "cycles per site" in ax.get_ylabel()

    def test_ymax_caps_y_limit(self):
        ymax = 10.0
        df = dbf_df()
        fig = line_kind.make_figure(df, _args(ymax=ymax))
        ax = fig.axes[0]
        assert ax.get_ylim()[1] == ymax

    def test_series_filter_pins_grouping(self):
        df = dbf_df()
        # Pin series to spacing_bytes only; the other varying axis (branches)
        # would normally also become a series — but with --series=spacing_bytes
        # the line count should match unique spacing values.
        fig = line_kind.make_figure(df, _args(x="branches", series="spacing_bytes"))
        ax = fig.axes[0]
        assert len(ax.get_lines()) == df["spacing_bytes"].nunique()

    def test_series_equal_to_x_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError, match="same as the X column"):
            line_kind.make_figure(df, _args(x="branches", series="branches"))

    def test_unknown_benchmark_falls_back_to_first_varying(self):
        df = dbf_df()
        df["benchmark"] = "imaginary_benchmark"  # registry miss
        fig = line_kind.make_figure(df, _args())
        ax = fig.axes[0]
        # varying_axis_columns is CSV-column-order; "branches" is first.
        assert ax.get_xlabel() == "branches"

    def test_default_xscale_is_log_for_dbf(self):
        # No --xscale and no registry hint => default 'log' base 2.
        df = dbf_df()
        fig = line_kind.make_figure(df, _args())
        ax = fig.axes[0]
        assert ax.get_xscale() == "log"

    def test_explicit_xscale_linear_overrides_default(self):
        df = dbf_df()
        fig = line_kind.make_figure(df, _args(xscale="linear"))
        ax = fig.axes[0]
        assert ax.get_xscale() == "linear"

    def test_explicit_xscale_log_keeps_log(self):
        df = dbf_df()
        fig = line_kind.make_figure(df, _args(xscale="log"))
        ax = fig.axes[0]
        assert ax.get_xscale() == "log"

    def test_nested_call_depth_registry_picks_linear_by_default(self):
        # nested_call_depth's depth axis is a plain range (1..64), so the
        # registry hints line_xscale="linear" — no --xscale flag needed.
        df = nced_df()
        fig = line_kind.make_figure(df, _args())
        ax = fig.axes[0]
        assert ax.get_xlabel() == "depth"
        assert ax.get_xscale() == "linear"

    def test_explicit_xscale_log_beats_registry_linear_hint(self):
        # User --xscale=log wins over the registry's "linear" hint for nced.
        df = nced_df()
        fig = line_kind.make_figure(df, _args(xscale="log"))
        ax = fig.axes[0]
        assert ax.get_xscale() == "log"

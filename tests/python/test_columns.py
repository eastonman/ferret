"""Tests for ferret_plot.columns and ferret_plot.errors."""

from __future__ import annotations

import numpy as np
import pytest
from ferret_plot import columns as cols
from ferret_plot.errors import PlotError
from ferret_plot.formatting import human_readable
from fixtures import dbf_df, dct_df


class TestMetadataCols:
    def test_includes_known_metadata(self):
        for name in (
            "benchmark",
            "ticks_min",
            "ticks_median",
            "iters",
            "sites_per_iter",
            "reps",
            "ns_per_site_min",
            "ns_per_site_median",
            "cycles_per_site_min",
            "cycles_per_site_median",
            "freq_hz",
        ):
            assert name in cols.METADATA_COLS

    def test_is_frozenset(self):
        # Must be immutable so callers can't mutate the project-wide set.
        assert isinstance(cols.METADATA_COLS, frozenset)


class TestResolveMetric:
    def test_auto_prefers_cycles_when_present(self):
        df = dbf_df(with_freq=True)
        m = cols.resolve_metric(df, metric="auto", stat="min")
        assert m.column == "cycles_per_site_min"
        assert m.short == "cycles"
        assert "min" in m.label

    def test_auto_falls_back_to_ns_when_cycles_absent(self):
        df = dbf_df(with_freq=False)
        m = cols.resolve_metric(df, metric="auto", stat="min")
        assert m.column == "ns_per_site_min"
        assert m.short == "ns"

    def test_auto_falls_back_to_ns_when_cycles_all_nan(self):
        df = dbf_df(with_freq=True)
        df["cycles_per_site_min"] = np.nan
        m = cols.resolve_metric(df, metric="auto", stat="min")
        assert m.column == "ns_per_site_min"

    def test_explicit_cycles_raises_when_missing(self):
        df = dbf_df(with_freq=False)
        with pytest.raises(PlotError):
            cols.resolve_metric(df, metric="cycles", stat="min")

    def test_explicit_ns_returns_ns_min(self):
        df = dbf_df(with_freq=True)
        m = cols.resolve_metric(df, metric="ns", stat="min")
        assert m.column == "ns_per_site_min"

    def test_stat_median_resolves_to_median_column(self):
        df = dbf_df(with_freq=True)
        m = cols.resolve_metric(df, metric="cycles", stat="median")
        assert m.column == "cycles_per_site_median"
        assert "median" in m.label

    def test_explicit_cycles_raises_when_all_nan(self):
        df = dbf_df(with_freq=True)
        df["cycles_per_site_min"] = np.nan
        with pytest.raises(PlotError):
            cols.resolve_metric(df, metric="cycles", stat="min")

    def test_invalid_stat_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError, match="stat must be"):
            cols.resolve_metric(df, metric="auto", stat="p99")

    def test_invalid_metric_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError, match="metric must be"):
            cols.resolve_metric(df, metric="ticks", stat="min")


class TestAxisColumns:
    def test_dbf_axes(self):
        df = dbf_df()
        assert cols.axis_columns(df) == ["branches", "spacing_bytes"]

    def test_dct_axes(self):
        df = dct_df()
        assert cols.axis_columns(df) == ["chain_length"]

    def test_varying_axis_columns_filters_constants(self):
        df = dbf_df(branches=(4,), spacing=(16, 32, 64))
        assert cols.varying_axis_columns(df) == ["spacing_bytes"]

    def test_varying_axis_columns_keeps_order(self):
        df = dbf_df()
        assert cols.varying_axis_columns(df) == ["branches", "spacing_bytes"]


class TestPlotError:
    def test_is_an_exception(self):
        assert issubclass(PlotError, Exception)

    def test_carries_message(self):
        e = PlotError("bad thing")
        assert str(e) == "bad thing"


class TestHumanReadable:
    def test_zero_passthrough(self):
        assert human_readable(0) == "0"

    def test_negative_passthrough(self):
        assert human_readable(-5) == "-5"

    def test_kilobyte_suffix(self):
        assert human_readable(2048) == "2K"

    def test_megabyte_suffix(self):
        assert human_readable(3 * (1 << 20)) == "3M"

    def test_string_returns_verbatim(self):
        assert human_readable("variant_a") == "variant_a"

    def test_bool_returns_str(self):
        # Without the bool guard, True would be treated as int 1 and printed as "1".
        # We want it as "True" so the rendering matches what pandas/csv would show.
        assert human_readable(True) == "True"

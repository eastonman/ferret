"""Tests for ferret_plot.registry."""

from __future__ import annotations

import pandas as pd
import pytest
from ferret_plot import registry as reg
from ferret_plot.errors import PlotError
from fixtures import dbf_df, dct_df, nced_df


class TestDetectBenchmark:
    def test_override_wins(self):
        df = dbf_df()
        assert reg.detect_benchmark(df, override="anything") == "anything"

    def test_reads_first_row_when_column_present(self):
        df = dbf_df()
        assert reg.detect_benchmark(df, override=None) == "direct_branch_footprint"

    def test_returns_none_when_column_absent(self):
        df = dbf_df().drop(columns=["benchmark"])
        assert reg.detect_benchmark(df, override=None) is None

    def test_returns_none_for_empty_frame(self):
        df = pd.DataFrame({"benchmark": []})
        assert reg.detect_benchmark(df, override=None) is None

    def test_mixed_benchmark_raises(self):
        df = pd.concat([dbf_df(), dct_df()], ignore_index=True)
        with pytest.raises(PlotError) as exc:
            reg.detect_benchmark(df, override=None)
        msg = str(exc.value)
        assert "direct_branch_footprint" in msg
        assert "dependent_chain_throughput" in msg


class TestResolveDefaults:
    def test_dbf_defaults(self):
        d = reg.resolve_defaults(dbf_df(), override=None)
        assert d.line_x == "branches"
        assert d.heatmap_x == "branches"
        assert d.heatmap_y == "spacing_bytes"

    def test_dct_defaults(self):
        d = reg.resolve_defaults(dct_df(), override=None)
        assert d.line_x == "chain_length"
        assert d.heatmap_x is None
        assert d.heatmap_y is None

    def test_unknown_benchmark_returns_empty_defaults(self):
        df = dct_df()
        df["benchmark"] = "imaginary_benchmark"
        d = reg.resolve_defaults(df, override=None)
        assert d.line_x is None
        assert d.heatmap_x is None
        assert d.heatmap_y is None

    def test_override_forces_known_benchmark(self):
        # CSV without a useful benchmark column; override forces registry lookup.
        df = pd.DataFrame({"benchmark": ["imaginary_benchmark"]})
        d = reg.resolve_defaults(df, override="direct_branch_footprint")
        assert d.line_x == "branches"
        assert d.heatmap_x == "branches"
        assert d.heatmap_y == "spacing_bytes"

    def test_nced_defaults(self):
        d = reg.resolve_defaults(nced_df(), override=None)
        assert d.line_x == "depth"
        assert d.line_xscale == "linear"
        # No heatmap defaults set — falls through to ordering rules if requested.
        assert d.heatmap_x is None
        assert d.heatmap_y is None


class TestBenchmarkDefaults:
    """Verify the dataclass exposes every expected default field."""

    def test_empty_defaults_have_all_none_fields(self):
        d = reg.BenchmarkDefaults()
        for attr in ("line_x", "line_xscale", "heatmap_x", "heatmap_y", "facet_col"):
            assert getattr(d, attr) is None


class TestRegistryHonesty:
    """Every column referenced in DEFAULTS must exist in its benchmark fixture."""

    _COLUMN_ATTRS = ("line_x", "heatmap_x", "heatmap_y", "facet_col")

    def test_dbf_columns_present(self):
        df = dbf_df()
        d = reg.DEFAULTS["direct_branch_footprint"]
        for attr in self._COLUMN_ATTRS:
            col = getattr(d, attr)
            if col is not None:
                assert col in df.columns, f"{attr}={col!r} not in dbf_df columns"

    def test_dct_columns_present(self):
        df = dct_df()
        d = reg.DEFAULTS["dependent_chain_throughput"]
        for attr in self._COLUMN_ATTRS:
            col = getattr(d, attr)
            if col is not None:
                assert col in df.columns, f"{attr}={col!r} not in dct_df columns"

    def test_nced_columns_present(self):
        df = nced_df()
        d = reg.DEFAULTS["nested_call_depth"]
        for attr in self._COLUMN_ATTRS:
            col = getattr(d, attr)
            if col is not None:
                assert col in df.columns, f"{attr}={col!r} not in nced_df columns"

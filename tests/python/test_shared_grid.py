"""Tests for shared 2D grid preparation used by heatmap-shaped plots."""

from __future__ import annotations

import argparse

import numpy as np
import pandas as pd
import plotly.graph_objects as go
import pytest
from ferret_plot.errors import PlotError
from ferret_plot.kinds._shared import (
    assert_finite_metric,
    assert_logz_positive,
    build_heatmap_trace,
    prepare_grid,
    resolve_heatmap_xy,
)
from ferret_plot.registry import BenchmarkDefaults
from fixtures import dbf_df

_MISSING_BRANCH = 2
_MISSING_SPACING = 32


class TestPrepareGrid:
    def test_grid_rows_are_y_and_columns_are_x(self):
        branches = (1, 2, 4)
        spacing = (16, 32)
        df = dbf_df(branches=branches, spacing=spacing)

        grid = prepare_grid(df, xcol="branches", ycol="spacing_bytes", value_col="cycles_per_site_min")

        assert grid.shape == (len(spacing), len(branches))
        assert grid.index.tolist() == list(spacing)
        assert grid.columns.tolist() == list(branches)

    def test_require_complete_raises_for_missing_cell(self):
        df = dbf_df(branches=(1, 2), spacing=(16, 32))
        df = df[~((df["branches"] == _MISSING_BRANCH) & (df["spacing_bytes"] == _MISSING_SPACING))]

        with pytest.raises(PlotError, match="missing grid cells"):
            prepare_grid(
                df,
                xcol="branches",
                ycol="spacing_bytes",
                value_col="cycles_per_site_min",
                require_complete=True,
            )

    def test_default_allows_missing_cell_for_existing_heatmap_behavior(self):
        df = dbf_df(branches=(1, 2), spacing=(16, 32))
        df = df[~((df["branches"] == _MISSING_BRANCH) & (df["spacing_bytes"] == _MISSING_SPACING))]

        grid = prepare_grid(df, xcol="branches", ycol="spacing_bytes", value_col="cycles_per_site_min")

        assert grid.isna().to_numpy().any()

    def test_require_complete_raises_for_all_nan_column(self):
        df = dbf_df(branches=(1, 2), spacing=(16, 32))
        df.loc[df["branches"] == _MISSING_BRANCH, "cycles_per_site_min"] = float("nan")

        with pytest.raises(PlotError, match="missing grid cells"):
            prepare_grid(
                df,
                xcol="branches",
                ycol="spacing_bytes",
                value_col="cycles_per_site_min",
                require_complete=True,
            )


class TestAssertFiniteMetric:
    def test_all_nan_raises(self):
        values = np.array([float("nan"), float("nan")])
        with pytest.raises(PlotError, match="no finite values"):
            assert_finite_metric(values, "my_metric")

    def test_finite_values_pass(self):
        values = np.array([1.0, 2.0, float("nan")])
        assert_finite_metric(values, "my_metric")  # no exception


class TestAssertLogzPositive:
    def test_all_nan_raises(self):
        values = np.array([float("nan"), float("nan")])
        with pytest.raises(PlotError, match="--logz requires at least one finite positive value"):
            assert_logz_positive(values)

    def test_non_positive_raises(self):
        values = np.array([0.0, 1.0, 2.0])
        with pytest.raises(PlotError, match="--logz requires"):
            assert_logz_positive(values)

    def test_negative_raises(self):
        values = np.array([-1.0, 2.0])
        with pytest.raises(PlotError, match="--logz requires"):
            assert_logz_positive(values)

    def test_positive_values_pass(self):
        values = np.array([0.5, 1.0, 2.0])
        assert_logz_positive(values)  # no exception

    def test_positive_with_nan_passes(self):
        values = np.array([1.0, float("nan"), 2.0])
        assert_logz_positive(values)  # no exception


class TestBuildHeatmapTrace:
    def test_returns_heatmap_trace(self):
        grid = pd.DataFrame(
            [[1.0, 2.0], [3.0, 4.0]],
            index=[10, 20],
            columns=[100, 200],
        )
        trace = build_heatmap_trace(grid, xcol="cols", ycol="rows", value_label="cycles", logz=False, cmap="Viridis")
        assert isinstance(trace, go.Heatmap)
        # Cells are placed at uniform index positions so log2 / power-of-2
        # sweeps render evenly; the caller adds tickvals/ticktext labels.
        assert list(trace.x) == [0, 1]
        assert list(trace.y) == [0, 1]
        np.testing.assert_array_equal(np.array(trace.z), grid.to_numpy())
        assert trace.colorscale is not None
        # Hover text carries the real axis values per cell.
        text = trace.text
        assert "cols=100" in text[0][0]
        assert "rows=10" in text[0][0]

    def test_logz_pre_transforms_z(self):
        grid = pd.DataFrame([[1.0, 10.0], [100.0, 1000.0]], index=[0, 1], columns=[0, 1])
        trace = build_heatmap_trace(grid, xcol="c", ycol="r", value_label="cycles", logz=True, cmap="Viridis")
        np.testing.assert_allclose(np.array(trace.z), np.log10(grid.to_numpy()))

    def test_coloraxis_attaches_to_shared_axis(self):
        grid = pd.DataFrame([[1.0]], index=[0], columns=[0])
        trace = build_heatmap_trace(
            grid,
            xcol="c",
            ycol="r",
            value_label="cycles",
            logz=False,
            cmap="Viridis",
            coloraxis="coloraxis",
        )
        assert trace.coloraxis == "coloraxis"
        # When attached to a shared coloraxis, the per-trace colorscale is not set.
        assert trace.colorscale is None

    def test_explicit_cmin_cmax_passed_through(self):
        grid = pd.DataFrame([[1.0, 2.0]], index=[0], columns=[0, 1])
        trace = build_heatmap_trace(
            grid,
            xcol="c",
            ycol="r",
            value_label="cycles",
            logz=False,
            cmap="Viridis",
            cmin=0.5,
            cmax=2.5,
        )
        assert trace.zmin == 0.5
        assert trace.zmax == 2.5


class TestResolveHeatmapXYConstantColumn:
    """Explicit --x or --y columns with a single unique value must be rejected."""

    def _args(self, *, x: str | None = None, y: str | None = None) -> argparse.Namespace:
        return argparse.Namespace(x=x, y=y)

    def _no_hints(self) -> BenchmarkDefaults:
        return BenchmarkDefaults()

    def test_constant_x_raises_plot_error(self):
        # 'branches' has only one unique value; 'spacing_bytes' varies.
        df = dbf_df(branches=(4,), spacing=(16, 32, 64))
        args = self._args(x="branches", y=None)
        with pytest.raises(PlotError, match="only one unique value"):
            resolve_heatmap_xy(df, args, self._no_hints())

    def test_constant_y_raises_plot_error(self):
        # 'spacing_bytes' has only one unique value; 'branches' varies.
        df = dbf_df(branches=(1, 2, 4), spacing=(32,))
        args = self._args(x=None, y="spacing_bytes")
        with pytest.raises(PlotError, match="only one unique value"):
            resolve_heatmap_xy(df, args, self._no_hints())

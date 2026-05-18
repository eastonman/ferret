"""Tests for shared 2D grid preparation used by heatmap-shaped plots."""

from __future__ import annotations

import pytest
from ferret_plot.errors import PlotError
from ferret_plot.kinds._shared import prepare_grid
from fixtures import dbf_df


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
        df = df[~((df["branches"] == 2) & (df["spacing_bytes"] == 32))]

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
        df = df[~((df["branches"] == 2) & (df["spacing_bytes"] == 32))]

        grid = prepare_grid(df, xcol="branches", ycol="spacing_bytes", value_col="cycles_per_site_min")

        assert grid.isna().to_numpy().any()

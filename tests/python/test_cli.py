"""End-to-end CLI tests for ferret_plot.cli."""

from __future__ import annotations

from pathlib import Path

import pandas as pd
import pytest
from ferret_plot import cli
from ferret_plot.cli import EXIT_USER_ERROR
from fixtures import dbf_df, dct_df, three_axis_df


def _write_csv(df, tmp_path: Path, name: str) -> str:
    p = tmp_path / name
    df.to_csv(p, index=False)
    return str(p)


class TestLineSubcommand:
    def test_line_produces_png(self, tmp_path):
        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "out.png"
        rc = cli.main(["line", csv_path, "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()
        assert out_path.stat().st_size > 0
        # PNG magic bytes.
        assert out_path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"

    def test_line_with_spacing_bytes_x(self, tmp_path):
        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "out.png"
        rc = cli.main(["line", csv_path, "--x", "spacing_bytes", "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()


class TestHeatmapSubcommand:
    def test_heatmap_produces_png(self, tmp_path):
        # 2-axis CSV is required.
        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "heat.png"
        rc = cli.main(["heatmap", csv_path, "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()
        assert out_path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"

    def test_heatmap_with_explicit_xy(self, tmp_path):
        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "heat-xy.png"
        rc = cli.main(
            [
                "heatmap",
                csv_path,
                "--x",
                "spacing_bytes",
                "--y",
                "branches",
                "--out",
                str(out_path),
            ]
        )
        assert rc == 0
        assert out_path.exists()


class TestFacetsSubcommand:
    def test_facets_produces_png(self, tmp_path):
        csv_path = _write_csv(three_axis_df(variants=("a", "b", "c")), tmp_path, "3ax.csv")
        out_path = tmp_path / "facets.png"
        rc = cli.main(
            [
                "facets",
                csv_path,
                "--facet",
                "variant",
                "--out",
                str(out_path),
            ]
        )
        assert rc == 0
        assert out_path.exists()
        assert out_path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"


class TestErrorPaths:
    def test_mixed_benchmark_csv_exits_2(self, tmp_path, capsys):
        mixed = pd.concat([dbf_df(), dct_df()], ignore_index=True)
        csv_path = _write_csv(mixed, tmp_path, "mixed.csv")
        rc = cli.main(["line", csv_path, "--out", str(tmp_path / "x.png")])
        assert rc == EXIT_USER_ERROR
        assert "mixed-benchmark CSV" in capsys.readouterr().err

    def test_missing_subcommand_exits_nonzero(self, tmp_path):
        with pytest.raises(SystemExit) as exc:
            cli.main([])  # no subcommand
        assert exc.value.code != 0

    def test_missing_csv_file_exits_2(self, tmp_path, capsys):
        rc = cli.main(["line", str(tmp_path / "nonexistent.csv"), "--out", str(tmp_path / "x.png")])
        assert rc == EXIT_USER_ERROR
        assert "No such" in capsys.readouterr().err

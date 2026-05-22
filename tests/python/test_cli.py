"""End-to-end CLI tests for ferret_plot.cli."""

from __future__ import annotations

from pathlib import Path

import pandas as pd
import pytest
from ferret_plot import cli
from ferret_plot.cli import EXIT_USER_ERROR
from fixtures import dbf_df, dct_df, tage_capacity_df, three_axis_df

_SURFACE_ELEV_DEFAULT = 20.0
_SURFACE_AZIM_DEFAULT = -2.0


def _write_csv(df, tmp_path: Path, name: str) -> str:
    p = tmp_path / name
    df.to_csv(p, index=False)
    return str(p)


class TestLineSubcommand:
    def test_line_produces_png(self, tmp_path, fake_png_export):
        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "out.png"
        rc = cli.main(["line", csv_path, "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()
        assert out_path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"

    def test_line_with_spacing_bytes_x(self, tmp_path, fake_png_export):
        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "out.png"
        rc = cli.main(["line", csv_path, "--x", "spacing_bytes", "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()

    def test_line_html_output(self, tmp_path):
        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "out.html"
        rc = cli.main(["line", csv_path, "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()
        body = out_path.read_text()
        assert "plotly" in body.lower()


class TestHeatmapSubcommand:
    def test_heatmap_produces_png(self, tmp_path, fake_png_export):
        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "heat.png"
        rc = cli.main(["heatmap", csv_path, "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()
        assert out_path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"

    def test_heatmap_with_explicit_xy(self, tmp_path, fake_png_export):
        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "heat-xy.png"
        rc = cli.main(["heatmap", csv_path, "--x", "spacing_bytes", "--y", "branches", "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()


class TestSurfaceSubcommand:
    def test_surface_parser_defaults(self):
        args = cli.build_parser().parse_args(["surface", "input.csv"])
        assert args.kind == "surface"
        assert args.x is None
        assert args.y is None
        assert args.logz is False
        assert args.elev == _SURFACE_ELEV_DEFAULT
        assert args.azim == _SURFACE_AZIM_DEFAULT

    def test_surface_produces_png(self, tmp_path, fake_png_export):
        csv_path = _write_csv(tage_capacity_df(), tmp_path, "tage.csv")
        out_path = tmp_path / "surface.png"
        rc = cli.main(
            [
                "surface",
                csv_path,
                "--x",
                "branch_amount",
                "--y",
                "pattern_amount",
                "--out",
                str(out_path),
            ]
        )
        assert rc == 0
        assert out_path.exists()
        assert out_path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
        assert fake_png_export["kwargs"]["format"] == "png"

    def test_surface_invalid_axis_exits_2(self, tmp_path, capsys):
        csv_path = _write_csv(tage_capacity_df(), tmp_path, "tage.csv")
        rc = cli.main(
            [
                "surface",
                csv_path,
                "--x",
                "not_a_column",
                "--y",
                "pattern_amount",
                "--out",
                str(tmp_path / "surface.png"),
            ]
        )
        assert rc == EXIT_USER_ERROR
        assert "not a column" in capsys.readouterr().err


class TestFacetsSubcommand:
    def test_facets_produces_png(self, tmp_path, fake_png_export):
        csv_path = _write_csv(three_axis_df(variants=("a", "b", "c")), tmp_path, "3ax.csv")
        out_path = tmp_path / "facets.png"
        rc = cli.main(["facets", csv_path, "--facet", "variant", "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()
        assert out_path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"


class TestMalformedCsv:
    def test_malformed_csv_exits_2(self, tmp_path, capsys):
        bad_csv = tmp_path / "bad.csv"
        bad_csv.write_text("a,b\n1,2\n3,4,5,extra\n")
        rc = cli.main(["line", str(bad_csv), "--out", str(tmp_path / "x.png")])
        assert rc == EXIT_USER_ERROR
        assert "malformed CSV" in capsys.readouterr().err


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

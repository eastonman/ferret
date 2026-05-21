"""End-to-end PNG export via kaleido.

Each test launches a real headless Chrome through kaleido, so the suite
is slow + dependency-heavy and lives behind the `integration` marker.
The gate mirrors `ferret_plot.output._find_chrome_executable` so a host
that satisfies the production probe also satisfies the test.

Run with: pytest -m integration.

The matrix exercises two PNG paths:

- heatmap: standard kaleido write_image (Plotly canvas)
- surface: WebGL fallback via headless Chromium SwiftShader, since
  kaleido fails on 3D scenes with the "error code 525" canvas error.
"""

from __future__ import annotations

from pathlib import Path

import pytest
from ferret_plot import cli
from ferret_plot.output import _find_chrome_executable
from fixtures import tage_capacity_df


def _chrome_present() -> bool:
    return _find_chrome_executable() is not None


_AXIS_ARGS = ["--x", "branch_amount", "--y", "pattern_amount"]


@pytest.mark.integration
@pytest.mark.skipif(not _chrome_present(), reason="kaleido needs Chrome on PATH")
@pytest.mark.parametrize(
    ("kind", "filename"),
    [
        ("heatmap", "heatmap.png"),
        ("surface", "surface.png"),
    ],
)
def test_png_round_trip(tmp_path: Path, kind: str, filename: str) -> None:
    csv_path = tmp_path / "tage.csv"
    tage_capacity_df().to_csv(csv_path, index=False)
    out = tmp_path / filename
    rc = cli.main([kind, str(csv_path), *_AXIS_ARGS, "--out", str(out)])
    assert rc == 0
    assert out.exists()
    assert out.stat().st_size > 1024  # nontrivial PNG, not just header
    assert out.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"

"""End-to-end image export via kaleido.

This test actually launches a headless Chrome through kaleido and is
slow + dependency-heavy. It's marked `integration` and skipped on
hosts without Chrome on PATH. Run with: pytest -m integration.
"""

from __future__ import annotations

import shutil
from pathlib import Path

import pytest
from ferret_plot import cli
from fixtures import tage_capacity_df

_CHROME_NAMES = (
    "chromium",
    "chromium-browser",
    "chrome",
    "Chrome",
    "google-chrome",
    "google-chrome-stable",
)


def _chrome_present() -> bool:
    return any(shutil.which(n) is not None for n in _CHROME_NAMES)


@pytest.mark.integration
@pytest.mark.skipif(not _chrome_present(), reason="kaleido needs Chrome on PATH")
def test_surface_png_round_trip(tmp_path: Path) -> None:
    csv_path = tmp_path / "tage.csv"
    tage_capacity_df().to_csv(csv_path, index=False)
    out = tmp_path / "surface.png"
    rc = cli.main(
        [
            "surface",
            str(csv_path),
            "--x",
            "branch_amount",
            "--y",
            "pattern_amount",
            "--out",
            str(out),
        ]
    )
    assert rc == 0
    assert out.exists()
    assert out.stat().st_size > 1024  # nontrivial PNG, not just header
    assert out.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"

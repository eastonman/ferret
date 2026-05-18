"""Shared pytest config for ferret_plot tests.

Adds scripts/ to sys.path so `import ferret_plot` works without an
editable install, and forces matplotlib's Agg backend before any test
imports it.
"""

import argparse
import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
import pytest

_SCRIPTS = Path(__file__).resolve().parent.parent.parent / "scripts"
sys.path.insert(0, str(_SCRIPTS))


# Per-kind default attribute sets — match the argparse namespaces built by
# ferret_plot.cli.build_parser. Update both sides when adding a flag.
_COMMON_DEFAULTS = {
    "csv": "",
    "out": None,
    "benchmark": None,
    "metric": "auto",
    "stat": "min",
}
_KIND_DEFAULTS = {
    "line": {"x": None, "xscale": None, "series": None, "ymax": None},
    "heatmap": {"x": None, "y": None, "logz": False},
    "facets": {"facet": None, "x": None, "y": None, "logz": False},
    "surface": {"x": None, "y": None, "logz": False, "elev": 30.0, "azim": -60.0},
}


def make_args(kind: str = "line", **overrides) -> argparse.Namespace:
    """Build an argparse.Namespace mirroring what the CLI hands to make_figure."""
    base = {**_COMMON_DEFAULTS, **_KIND_DEFAULTS[kind]}
    base.update(overrides)
    return argparse.Namespace(**base)


@pytest.fixture(autouse=True)
def _close_mpl_figures():
    """Close every matplotlib figure after each test to prevent leak warnings
    and cross-test state bleed."""
    yield
    plt.close("all")

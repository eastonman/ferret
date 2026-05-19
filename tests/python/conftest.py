"""Shared pytest config for ferret_plot tests.

Adds scripts/ to sys.path so `import ferret_plot` works without an
editable install.
"""

import argparse
import sys
from pathlib import Path

_SCRIPTS = Path(__file__).resolve().parent.parent.parent / "scripts"
sys.path.insert(0, str(_SCRIPTS))


# Per-kind default attribute sets — match the argparse namespaces built by
# ferret_plot.cli.build_parser. Update both sides when adding a flag.
_COMMON_DEFAULTS = {
    "csv": "",
    "out": None,
    "format": None,
    "html_js": "cdn",
    "benchmark": None,
    "metric": "auto",
    "stat": "min",
}
# `cmap` is registered per-kind by the migrating tasks (Tasks 5-7).
# Listed here ahead of time so make_args(...) won't AttributeError
# when those tasks land.
_KIND_DEFAULTS = {
    "line": {"x": None, "xscale": None, "series": None, "ymax": None},
    "heatmap": {"x": None, "y": None, "logz": False, "cmap": None},
    "facets": {"facet": None, "x": None, "y": None, "logz": False, "cmap": None},
    "surface": {"x": None, "y": None, "logz": False, "elev": 20.0, "azim": -13.0, "cmap": None},
}


def make_args(kind: str = "line", **overrides) -> argparse.Namespace:
    """Build an argparse.Namespace mirroring what the CLI hands to make_figure."""
    base = {**_COMMON_DEFAULTS, **_KIND_DEFAULTS[kind]}
    base.update(overrides)
    return argparse.Namespace(**base)

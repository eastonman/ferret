"""Shared pytest config for ferret_plot tests.

Adds scripts/ to sys.path so `import ferret_plot` works without an
editable install.
"""

import argparse
import sys
from pathlib import Path

import pytest

_SCRIPTS = Path(__file__).resolve().parent.parent.parent / "scripts"
sys.path.insert(0, str(_SCRIPTS))

from ferret_plot import cli as _cli
from ferret_plot import output


# Destinations shared by every subparser — worth declaring explicitly so
# tests can override them without knowing a subcommand's specific flags.
_COMMON_DEFAULTS = {
    "csv": "",
    "out": None,
    "format": None,
    "html_js": "cdn",
    "benchmark": None,
    "metric": "auto",
    "stat": "min",
}


def _introspect_kind_defaults() -> dict[str, dict[str, object]]:
    """Derive per-subcommand default dicts from the live parser.

    Walks every subparser and collects the `default` of each _StoreAction,
    skipping the destinations already covered by _COMMON_DEFAULTS and the
    internal `handler` default injected by set_defaults.
    """
    import argparse as _ap

    skip = set(_COMMON_DEFAULTS) | {"handler", "kind"}
    result: dict[str, dict[str, object]] = {}
    parser = _cli.build_parser()
    for action in parser._actions:
        if not isinstance(action, _ap._SubParsersAction):
            continue
        for name, subparser in action.choices.items():
            defaults: dict[str, object] = {}
            for sub_action in subparser._actions:
                if sub_action.dest in skip:
                    continue
                if sub_action.dest == argparse.SUPPRESS:
                    continue
                defaults[sub_action.dest] = sub_action.default
            result[name] = defaults
    return result


_KIND_DEFAULTS = _introspect_kind_defaults()


def make_args(kind: str = "line", **overrides) -> argparse.Namespace:
    """Build an argparse.Namespace mirroring what the CLI hands to make_figure."""
    base = {**_COMMON_DEFAULTS, **_KIND_DEFAULTS[kind]}
    base.update(overrides)
    return argparse.Namespace(**base)


@pytest.fixture(autouse=True)
def _clear_chrome_cache():
    output._chrome_probe_cache.clear()
    yield
    output._chrome_probe_cache.clear()


@pytest.fixture
def fake_png_export(monkeypatch):
    """Stub out PNG export so tests do not require a real Chrome/Kaleido binary.

    Patches _chrome_available to return True, clears the probe cache, and
    replaces Figure.write_image with a stub that writes a minimal valid PNG
    header and records the last call in the returned `captured` dict.
    """
    captured: dict = {}

    def _fake_write_image(self, path, **kwargs):
        captured["path"] = path
        captured["kwargs"] = kwargs
        with open(path, "wb") as f:
            f.write(b"\x89PNG\r\n\x1a\n" + b"\x00" * 16)

    monkeypatch.setattr(output, "_chrome_available", lambda: True)
    output._chrome_probe_cache.clear()
    monkeypatch.setattr("plotly.graph_objects.Figure.write_image", _fake_write_image)
    yield captured

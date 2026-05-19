"""Output emit for plotly figures.

Resolves `(out, fmt, html_js)` to a concrete write target:

- `out` ending in .html / .png / .svg / .pdf / .jpg / .webp infers
  the format from the extension.
- `fmt` explicit overrides the extension.
- `out` omitted writes a temp HTML and opens it in the system browser.
- Anything else raises `PlotError`.

The first image export per process probes for a Chrome/Chromium
binary on PATH and raises `PlotError` with an install hint on miss,
so users don't see kaleido's internal traceback. Result is cached.
"""

from __future__ import annotations

import os
import shutil
import tempfile
import webbrowser
from pathlib import Path
from typing import Any

from ferret_plot.errors import PlotError

_IMAGE_FORMATS = frozenset({"png", "svg", "pdf", "jpg", "jpeg", "webp"})
_HTML_FORMATS = frozenset({"html"})
_KNOWN_FORMATS = _IMAGE_FORMATS | _HTML_FORMATS
_HTML_JS_MAP = {"cdn": "cdn", "inline": True, "sibling": "directory"}

# Names kaleido v1 checks on PATH for Chrome/Chromium.
_CHROME_NAMES = (
    "chromium",
    "chromium-browser",
    "chrome",
    "Chrome",
    "google-chrome",
    "google-chrome-stable",
)

# Standard macOS Chrome.app install locations. Brew casks drop the bundle
# at /Applications/ without adding a binary to PATH, so shutil.which alone
# misses them.
_MACOS_CHROME_PATHS = (
    "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    "/Applications/Google Chrome for Testing.app/Contents/MacOS/Google Chrome for Testing",
    "/Applications/Chromium.app/Contents/MacOS/Chromium",
)

_INSTALL_HINT = (
    "kaleido needs Chrome or Chromium installed. "
    "Nix devshell on Linux ships pkgs.chromium; on macOS run "
    "`brew install --cask google-chrome` then point BROWSER_PATH at "
    "the binary, or run `python -m plotly.io._kaleido install_chrome` "
    "to install a portable headless chrome."
)

# Module-level cache so we probe at most once per process.
_chrome_probe_cache: dict[str, bool] = {}


def _kaleido_bundles_chrome() -> bool:
    """Kaleido v0.x ships its own Chromium; v1+ drives an external one."""
    try:
        import kaleido  # noqa: PLC0415  # lazy so missing kaleido is a soft fail
    except ImportError:
        return False
    try:
        return int(getattr(kaleido, "__version__", "1.0").split(".")[0]) < 1
    except (ValueError, AttributeError):
        return False


def _chrome_available() -> bool:
    """Return True if kaleido has a usable Chrome to drive.

    Mirrors kaleido v1's own search so the pre-check matches what it
    can actually find: BROWSER_PATH override, PATH lookup against
    `_CHROME_NAMES`, and the standard macOS app-bundle locations.
    Kaleido v0.x bundles its own Chromium, so the probe short-circuits
    to True when v0 is installed.
    """
    if _kaleido_bundles_chrome():
        return True
    browser_path = os.environ.get("BROWSER_PATH")
    if browser_path and os.path.isfile(browser_path) and os.access(browser_path, os.X_OK):
        return True
    if any(shutil.which(name) is not None for name in _CHROME_NAMES):
        return True
    return any(os.path.isfile(p) and os.access(p, os.X_OK) for p in _MACOS_CHROME_PATHS)


def _resolve_format(out: str | None, fmt: str | None) -> str:
    if fmt is not None:
        if fmt not in _KNOWN_FORMATS:
            raise PlotError(f"--format={fmt!r} is not supported; pick one of {sorted(_KNOWN_FORMATS)}")
        return fmt
    if out is None:
        return "html"  # no --out => temp HTML
    ext = Path(out).suffix.lower().lstrip(".")
    if ext in _KNOWN_FORMATS:
        return ext
    raise PlotError(
        f"unrecognized output extension {ext!r} (from --out={out!r}); pass --format={'|'.join(sorted(_KNOWN_FORMATS))}"
    )


def _check_chrome() -> None:
    """Probe once per process. Subsequent calls hit the cache."""
    if "ok" in _chrome_probe_cache:
        if not _chrome_probe_cache["ok"]:
            raise PlotError(_INSTALL_HINT)
        return
    ok = _chrome_available()
    _chrome_probe_cache["ok"] = ok
    if not ok:
        raise PlotError(_INSTALL_HINT)


def _image_size(fig: Any) -> tuple[int, int]:
    """Pick width/height for image export.

    Plotly: width/height in px, scale=2 => final px = w*scale x h*scale.

    Surface plots size the canvas to match the scene's aspectratio so a
    wide grid renders as a wide PNG (no wasted dead space). Long edge
    is capped at 2400 px to keep file sizes reasonable. Non-surface
    traces get a fixed wide canvas.
    """
    for trace in fig.data:
        if getattr(trace, "type", "") == "surface":
            return _surface_canvas(fig)
    return 1600, 1000


def _surface_canvas(fig: Any) -> tuple[int, int]:
    """Derive surface PNG dimensions from scene.aspectratio."""
    long_edge = 2400
    short_edge_min = 1000  # never go more square than this
    aspect = getattr(fig.layout.scene, "aspectratio", None)
    if aspect is None or not getattr(aspect, "x", None) or not getattr(aspect, "y", None):
        return long_edge, short_edge_min
    ax, ay = float(aspect.x), float(aspect.y)
    # Project the (x, y) ground plane to the screen. We don't know the
    # camera projection exactly, but for an isometric-ish view the wider
    # of the two horizontal axes dominates the screen width.
    ratio = max(ax, ay) / min(ax, ay)
    width = long_edge
    height = max(short_edge_min, int(long_edge / max(ratio, 1.0)))
    return width, height


def emit(fig: Any, *, out: str | None, fmt: str | None, html_js: str) -> None:
    """Write `fig` to `out` (or open in browser if `out is None`).

    fig is a plotly.graph_objects.Figure.
    fmt overrides the extension on `out`. html_js is one of {"cdn",
    "inline", "sibling"} and only affects HTML output.
    """
    if html_js not in _HTML_JS_MAP:
        raise PlotError(f"--html-js={html_js!r} must be one of {sorted(_HTML_JS_MAP)}")

    resolved = _resolve_format(out, fmt)

    if resolved == "html":
        if out is None:
            with tempfile.NamedTemporaryFile(suffix=".html", delete=False) as tmp:
                out_path = tmp.name
        else:
            out_path = out
        fig.write_html(
            out_path,
            include_plotlyjs=_HTML_JS_MAP[html_js],
            full_html=True,
        )
        if out is None:
            webbrowser.open(f"file://{out_path}")
        return

    # Image format => kaleido path. Probe Chrome first.
    _check_chrome()
    width, height = _image_size(fig)
    fig.write_image(out, format=resolved, width=width, height=height, scale=2)

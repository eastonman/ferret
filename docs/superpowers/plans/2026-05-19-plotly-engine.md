# Plotly Plot Engine Migration Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace matplotlib with plotly + kaleido as the rendering engine for all four `scripts/plot.py` kinds (`surface`, `heatmap`, `facets`, `line`), gaining per-pixel smooth terrain shading on the 3D surface and interactive HTML hover tooltips on the 2D plots.

**Architecture:** Four-layer split. (1) Each `kinds/*.py` returns a `plotly.graph_objects.Figure`. (2) A new `output.py` module owns format resolution, HTML/image writing, and the Chrome probe. (3) `formatting.py` keeps `decimate_indices` + `human_readable` (engine-neutral). (4) `cli.py` dispatches. Migration is staged: shared infrastructure first, then surface (visual motivator + proves the scaffolding), then heatmap, facets, line; matplotlib is removed only after all four kinds are off it. During staging, `output.emit` dispatches on figure type so non-migrated kinds keep working.

**Tech Stack:** Python 3.11+, pandas, numpy, plotly ≥ 6.1.1, kaleido ≥ 1.0, pytest, ruff. Nix dev shell provides `pkgs.chromium` for kaleido's headless export.

**Spec:** `docs/superpowers/specs/2026-05-19-plotly-engine-design.md`

**Baseline:** `1c1e718` on branch `plotly`.

---

## File Structure

- Modify `requirements.txt`: add `plotly>=6.1.1` and `kaleido>=1.0`; drop `matplotlib` in the final task.
- Modify `flake.nix`: add `plotly` + `kaleido` to the Python package list; add `pkgs.chromium`; drop `matplotlib` in the final task.
- Create `scripts/ferret_plot/output.py`: `emit(fig, *, out, fmt, html_js)` resolves format from extension/override, writes HTML or static image, opens a temp HTML when `out` is omitted, probes for Chrome on first image export.
- Modify `scripts/ferret_plot/cli.py`: add `--format`, `--html-js` to `_add_common`; per-kind `--cmap`; replace inline `_emit` with `output.emit`.
- Modify `scripts/ferret_plot/formatting.py`: drop `apply_axis` (matplotlib-specific) in the final task; keep `decimate_indices` and `human_readable`.
- Modify `scripts/ferret_plot/kinds/_shared.py`: add `build_heatmap_trace(grid, ...)` (plotly) alongside `render_heatmap_cell` (matplotlib) during staging; drop the latter in the final task.
- Modify `scripts/ferret_plot/kinds/surface.py`: return a `plotly.graph_objects.Figure` built from `go.Surface`; default colorscale `Earth`.
- Modify `scripts/ferret_plot/kinds/heatmap.py`: return a plotly figure built from `build_heatmap_trace`.
- Modify `scripts/ferret_plot/kinds/facets.py`: return a plotly subplot grid sharing one `coloraxis`.
- Modify `scripts/ferret_plot/kinds/line.py`: return a plotly figure of `go.Scatter`/`go.Scattergl` traces.
- Modify `tests/python/conftest.py`: add `format=None`, `html_js="cdn"`, per-kind `cmap=None` to `make_args` defaults; drop the matplotlib backend setup and the `_close_mpl_figures` fixture in the final task.
- Create `tests/python/test_output.py`: cover `emit` resolution rules, `--html-js` mapping, Chrome-missing path, no-`--out` temp-HTML path.
- Modify `tests/python/test_surface.py`, `test_heatmap.py`, `test_facets.py`, `test_line.py`: switch from matplotlib axis assertions to plotly figure-dict assertions; monkeypatch `fig.write_image` to avoid needing Chrome.
- Modify `tests/python/test_cli.py`: keep PNG end-to-end assertions but mock `fig.write_image` to produce a non-empty file without launching Chrome; add `.html` and `--format` cases.
- Create `tests/python/test_integration_export.py`: one `@pytest.mark.integration` test gated on Chrome that runs an actual end-to-end PNG export.
- Modify `README.md`: Quickstart shows `.png` and `.html` output; one-line Chrome note.
- Modify `docs/cli.md`: document `--format`, `--html-js`, `--cmap`; add an "Output formats" subsection.

---

## Pre-Flight

- [ ] **Step 0.1: Verify the working tree is clean**

Run:

```bash
git status --short
```

Expected: no tracked source changes. If there are any, ask the user before proceeding.

- [ ] **Step 0.2: Confirm baseline commit**

Run:

```bash
git rev-parse --short HEAD
```

Expected: `1c1e718` (or a later commit if a prior task already landed). The plan assumes branch `plotly`.

- [ ] **Step 0.3: Verify Python tooling**

Run:

```bash
python3 -c "import pandas, numpy; print(pandas.__version__, numpy.__version__)"
python3 -m pytest --version
```

Expected: both commands exit 0 and print versions.

- [ ] **Step 0.4: Verify current Python tests pass on baseline**

Run:

```bash
./scripts/test_py.sh
```

Expected: all tests pass. Capture the test count — it should match after the migration (with new tests added on top).

---

## Task 1: Add plotly and kaleido dependencies

**Files:**
- Modify: `requirements.txt`
- Modify: `flake.nix:37-45`

- [ ] **Step 1.1: Update `requirements.txt`**

Replace its contents with:

```
# Runtime deps for scripts/ — Nix users: see flake.nix.
matplotlib
numpy
pandas
plotly>=6.1.1
kaleido>=1.0
```

(Matplotlib is kept for now; it goes away in Task 9 after all kinds have migrated.)

- [ ] **Step 1.2: Update `flake.nix` Python package list**

In the `python3.withPackages` block at `flake.nix:37-45`, add `plotly` and `kaleido` after `pandas`:

```nix
            (pkgs.python3.withPackages (
              ps:
                with ps; [
                  matplotlib
                  numpy
                  pandas
                  plotly
                  kaleido
                  pytest
                ]
            ))
```

If `kaleido` v1 is missing or too old in your nixpkgs pin, leave the line in and add a follow-up note in the PR description; users can `pip install --user kaleido>=1.0` inside the dev shell as a fallback. Don't pin to an older `kaleido` — v0 bundles Chrome and we don't want that.

- [ ] **Step 1.3: Add `pkgs.chromium` to the dev shell**

In the `packages = [...]` list at `flake.nix:27-46`, add `pkgs.chromium` after `pkgs.cli11`:

```nix
            pkgs.cli11
            pkgs.chromium
            pkgs.gtest
```

- [ ] **Step 1.4: Verify imports work**

Run:

```bash
python3 -c "import plotly; import kaleido; print(plotly.__version__, kaleido.__version__)"
```

Expected: both print version strings. Plotly ≥ 6.1.1, kaleido ≥ 1.0.

If the command fails outside the Nix dev shell, run it inside `nix develop` instead. If kaleido is missing from nixpkgs, install once into the local venv: `pip install --user 'kaleido>=1.0'`.

- [ ] **Step 1.5: Commit**

```bash
git add requirements.txt flake.nix
git commit --no-gpg-sign -m "$(cat <<'EOF'
build(plot): add plotly + kaleido dependencies

Phase 1 of the plot engine migration: pull in plotly>=6.1.1 and
kaleido>=1.0 and add pkgs.chromium to the Nix dev shell so kaleido's
headless export can find a browser. Matplotlib stays for now and is
removed in Task 9 once all four plot kinds are on plotly.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 2: Create `output.py` with `emit()` and Chrome probe

**Files:**
- Create: `scripts/ferret_plot/output.py`
- Create: `tests/python/test_output.py`

- [ ] **Step 2.1: Write the failing tests for `emit` format resolution**

Create `tests/python/test_output.py` with:

```python
"""Tests for ferret_plot.output.emit."""

from __future__ import annotations

from unittest.mock import MagicMock

import plotly.graph_objects as go
import pytest

from ferret_plot import output
from ferret_plot.errors import PlotError


def _fig() -> go.Figure:
    return go.Figure(data=[go.Scatter(x=[0, 1], y=[0, 1])])


class TestEmitFormatResolution:
    def test_html_extension_routes_to_write_html(self, tmp_path, monkeypatch):
        fig = _fig()
        mock = MagicMock()
        monkeypatch.setattr(fig, "write_html", mock)
        output.emit(fig, out=str(tmp_path / "x.html"), fmt=None, html_js="cdn")
        mock.assert_called_once()
        kwargs = mock.call_args.kwargs
        assert kwargs["include_plotlyjs"] == "cdn"
        assert kwargs["full_html"] is True

    def test_png_extension_routes_to_write_image(self, tmp_path, monkeypatch):
        fig = _fig()
        mock = MagicMock()
        monkeypatch.setattr(fig, "write_image", mock)
        monkeypatch.setattr(output, "_chrome_available", lambda: True)
        output.emit(fig, out=str(tmp_path / "x.png"), fmt=None, html_js="cdn")
        mock.assert_called_once()
        assert mock.call_args.kwargs["format"] == "png"

    def test_format_overrides_extension(self, tmp_path, monkeypatch):
        fig = _fig()
        mock = MagicMock()
        monkeypatch.setattr(fig, "write_image", mock)
        monkeypatch.setattr(output, "_chrome_available", lambda: True)
        output.emit(fig, out=str(tmp_path / "x.html"), fmt="png", html_js="cdn")
        mock.assert_called_once()
        assert mock.call_args.kwargs["format"] == "png"

    def test_no_out_writes_temp_html_and_opens_browser(self, monkeypatch):
        fig = _fig()
        opened = []
        monkeypatch.setattr(output.webbrowser, "open", lambda url: opened.append(url))
        output.emit(fig, out=None, fmt=None, html_js="cdn")
        assert len(opened) == 1
        assert opened[0].endswith(".html")

    def test_unknown_extension_without_format_raises(self, tmp_path):
        fig = _fig()
        with pytest.raises(PlotError, match="unrecognized output extension"):
            output.emit(fig, out=str(tmp_path / "x.foo"), fmt=None, html_js="cdn")


class TestHtmlJsMapping:
    @pytest.mark.parametrize(
        "flag,expected",
        [("cdn", "cdn"), ("inline", True), ("sibling", "directory")],
    )
    def test_html_js_maps_correctly(self, flag, expected, tmp_path, monkeypatch):
        fig = _fig()
        mock = MagicMock()
        monkeypatch.setattr(fig, "write_html", mock)
        output.emit(fig, out=str(tmp_path / "x.html"), fmt=None, html_js=flag)
        assert mock.call_args.kwargs["include_plotlyjs"] == expected


class TestChromeProbe:
    def test_chrome_missing_raises_plot_error(self, tmp_path, monkeypatch):
        fig = _fig()
        monkeypatch.setattr(output, "_chrome_available", lambda: False)
        # Bust the cache so the test sees the patched value.
        output._chrome_probe_cache.clear()
        with pytest.raises(PlotError, match="Chrome or Chromium"):
            output.emit(fig, out=str(tmp_path / "x.png"), fmt=None, html_js="cdn")
```

- [ ] **Step 2.2: Run the tests to verify they fail**

Run:

```bash
python3 -m pytest tests/python/test_output.py -v
```

Expected: `ImportError: cannot import name 'output' from 'ferret_plot'`.

- [ ] **Step 2.3: Implement `output.py`**

Create `scripts/ferret_plot/output.py`:

```python
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

# Names kaleido checks on PATH for Chrome/Chromium.
_CHROME_NAMES = (
    "chromium",
    "chromium-browser",
    "chrome",
    "Chrome",
    "google-chrome",
    "google-chrome-stable",
)

_INSTALL_HINT = (
    "kaleido needs Chrome or Chromium installed. "
    "In the Nix devshell this is provided automatically. "
    "Otherwise run `python -m plotly.io._kaleido install_chrome` "
    "or install via your package manager."
)

# Module-level cache so we probe at most once per process.
_chrome_probe_cache: dict[str, bool] = {}


def _chrome_available() -> bool:
    """Return True if any Chromium-class binary is on PATH."""
    return any(shutil.which(name) is not None for name in _CHROME_NAMES)


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
        f"unrecognized output extension {ext!r} (from --out={out!r}); "
        f"pass --format=html|png|svg|pdf|jpg|webp"
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
    """Pick width/height for image export, matching today's matplotlib figsizes.

    Today: surface uses figsize=(10, 8), others (8, 5), all dpi=400.
    Plotly: width/height in px, scale=2 ⇒ final px = w*scale × h*scale.
    """
    # 3D surface => taller; everything else => wide.
    for trace in fig.data:
        if getattr(trace, "type", "") == "surface":
            return 2000, 1600
    return 1600, 1000


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
            tmp = tempfile.NamedTemporaryFile(suffix=".html", delete=False)
            tmp.close()
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

    # Image format → kaleido path. Probe Chrome first.
    _check_chrome()
    width, height = _image_size(fig)
    fig.write_image(out, format=resolved, width=width, height=height, scale=2)
```

- [ ] **Step 2.4: Run the tests to verify they pass**

Run:

```bash
python3 -m pytest tests/python/test_output.py -v
```

Expected: 7 tests pass. If `test_no_out_writes_temp_html_and_opens_browser` fails because `webbrowser.open` is not in `output.webbrowser`, that's a module-attribute path mismatch — adjust the monkeypatch to `output.webbrowser.open` (the import is `import webbrowser`, so `output.webbrowser` exists).

- [ ] **Step 2.5: Commit**

```bash
git add scripts/ferret_plot/output.py tests/python/test_output.py
git commit --no-gpg-sign -m "$(cat <<'EOF'
feat(plot): add plotly output emit module

emit() resolves format from --out extension or explicit --format,
writes HTML via write_html or static images via write_image, and
falls back to a temp HTML + system browser when --out is omitted.
First image export probes for Chrome on PATH and raises a clear
PlotError with the install hint on miss (cached per process).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 3: Wire output.emit into CLI; add `--format` and `--html-js` flags

**Files:**
- Modify: `scripts/ferret_plot/cli.py`
- Modify: `tests/python/conftest.py:25-37`

This task wires `output.emit` into the CLI without migrating any kind yet. Matplotlib figures still flow through; we extend `emit` to accept them transitionally.

- [ ] **Step 3.1: Add `matplotlib` Figure passthrough to `emit`**

Modify `scripts/ferret_plot/output.py`. At the top of `emit(...)`, add a matplotlib-figure dispatch *before* the html_js check (so legacy matplotlib kinds keep working until they migrate). Insert this after the docstring:

```python
    # Transitional: until all kinds migrate to plotly, emit also accepts
    # a matplotlib Figure and falls back to fig.savefig. This branch is
    # removed in Task 9.
    if _is_mpl_figure(fig):
        return _emit_mpl(fig, out=out, fmt=fmt)
```

And add these helpers at the bottom of the file:

```python
def _is_mpl_figure(fig: Any) -> bool:
    return type(fig).__module__.startswith("matplotlib.")


def _emit_mpl(fig: Any, *, out: str | None, fmt: str | None) -> None:
    """Matplotlib fallback. Mirrors the previous cli._emit behavior."""
    import matplotlib.pyplot as plt

    if fmt == "html":
        raise PlotError("HTML output is not supported for this plot kind yet (matplotlib backend)")
    if out is None:
        plt.show()
        return
    fig.savefig(out, bbox_inches="tight", dpi=400)
    plt.close(fig)
```

- [ ] **Step 3.2: Add a test for the matplotlib passthrough**

Add to `tests/python/test_output.py`:

```python
class TestMatplotlibPassthrough:
    def test_mpl_figure_savefig_path(self, tmp_path, monkeypatch):
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        fig = plt.figure()
        fig.add_subplot(111).plot([0, 1], [0, 1])
        try:
            out = tmp_path / "mpl.png"
            output.emit(fig, out=str(out), fmt=None, html_js="cdn")
            assert out.exists() and out.stat().st_size > 0
        finally:
            plt.close(fig)

    def test_mpl_figure_html_format_raises(self, tmp_path, monkeypatch):
        import matplotlib

        matplotlib.use("Agg")
        import matplotlib.pyplot as plt

        fig = plt.figure()
        try:
            with pytest.raises(PlotError, match="not supported for this plot kind yet"):
                output.emit(fig, out=str(tmp_path / "x.html"), fmt=None, html_js="cdn")
        finally:
            plt.close(fig)
```

Run:

```bash
python3 -m pytest tests/python/test_output.py -v
```

Expected: all tests pass.

- [ ] **Step 3.3: Update `cli.py` to use `output.emit`**

Modify `scripts/ferret_plot/cli.py`:

Replace the `_add_common` function with:

```python
def _add_common(sp: argparse.ArgumentParser) -> None:
    sp.add_argument("csv")
    sp.add_argument("--out", default=None, help="output image path; omitted = open HTML in browser")
    sp.add_argument(
        "--format",
        default=None,
        choices=["html", "png", "svg", "pdf", "jpg", "webp"],
        help="output format override (default: infer from --out extension)",
    )
    sp.add_argument(
        "--html-js",
        dest="html_js",
        default="cdn",
        choices=["cdn", "inline", "sibling"],
        help="how to bundle plotly.js in HTML output (default: cdn)",
    )
    sp.add_argument("--benchmark", default=None, help="override registry lookup (rare)")
    sp.add_argument("--metric", default="auto", choices=["auto", "cycles", "ns"])
    sp.add_argument("--stat", default="min", choices=["min", "median"])
```

Replace the `_emit` function and `main`:

```python
def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        df = pd.read_csv(args.csv)
        fig = args.handler(df, args)
        output.emit(fig, out=args.out, fmt=args.format, html_js=args.html_js)
        return 0
    except (PlotError, FileNotFoundError) as e:
        print(f"plot.py: {e}", file=sys.stderr)
        return EXIT_USER_ERROR
```

Add the import at the top of the file:

```python
from ferret_plot import output
```

Delete the old `_emit` function and the `import matplotlib`, `matplotlib.use("Agg")`, `import matplotlib.pyplot as plt`, `from matplotlib.figure import Figure` lines at the top of `cli.py`. After this step, `cli.py` should have no matplotlib imports (the kinds still import matplotlib themselves; that's fine until they migrate).

- [ ] **Step 3.4: Update `conftest.py` defaults**

Modify `tests/python/conftest.py:25-37`. Replace `_COMMON_DEFAULTS` and the per-kind defaults:

```python
_COMMON_DEFAULTS = {
    "csv": "",
    "out": None,
    "format": None,
    "html_js": "cdn",
    "benchmark": None,
    "metric": "auto",
    "stat": "min",
}
_KIND_DEFAULTS = {
    "line": {"x": None, "xscale": None, "series": None, "ymax": None},
    "heatmap": {"x": None, "y": None, "logz": False, "cmap": None},
    "facets": {"facet": None, "x": None, "y": None, "logz": False, "cmap": None},
    "surface": {"x": None, "y": None, "logz": False, "elev": 30.0, "azim": -60.0, "cmap": None},
}
```

(`cmap=None` is added here so the migrated kinds default to their per-kind colorscale when the test doesn't override it. Non-migrated kinds ignore it.)

- [ ] **Step 3.5: Run the full test suite**

Run:

```bash
./scripts/test_py.sh
```

Expected: all existing tests still pass (CLI dispatch goes through `output.emit`, which detects the matplotlib figures returned by the still-unmigrated kinds and falls back to `savefig`). The new `test_output.py` tests also pass.

- [ ] **Step 3.6: Commit**

```bash
git add scripts/ferret_plot/output.py scripts/ferret_plot/cli.py tests/python/conftest.py tests/python/test_output.py
git commit --no-gpg-sign -m "$(cat <<'EOF'
feat(plot): wire output.emit into CLI; add --format / --html-js

The CLI now hands every figure to output.emit, which detects the
figure type and dispatches: matplotlib figures fall through to
savefig (unchanged behavior); plotly figures use the new emit path.
emit lands now so kinds can migrate one at a time without further
CLI churn. The matplotlib passthrough disappears in Task 9.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 4: Add `build_heatmap_trace` to `_shared.py`

**Files:**
- Modify: `scripts/ferret_plot/kinds/_shared.py`
- Modify: `tests/python/test_shared_grid.py`

`render_heatmap_cell` stays in place during staging (still used by the matplotlib `heatmap` and `facets`). `build_heatmap_trace` is added alongside.

- [ ] **Step 4.1: Write the failing tests for `build_heatmap_trace`**

Append to `tests/python/test_shared_grid.py`:

```python
import numpy as np
import plotly.graph_objects as go

from ferret_plot.kinds._shared import build_heatmap_trace


class TestBuildHeatmapTrace:
    def test_returns_heatmap_trace(self):
        grid = pd.DataFrame(
            [[1.0, 2.0], [3.0, 4.0]],
            index=[10, 20],
            columns=[100, 200],
        )
        trace = build_heatmap_trace(grid, value_label="cycles", logz=False, cmap="Viridis")
        assert isinstance(trace, go.Heatmap)
        assert list(trace.x) == [100, 200]
        assert list(trace.y) == [10, 20]
        np.testing.assert_array_equal(np.array(trace.z), grid.to_numpy())
        assert trace.colorscale[0][1].startswith("#") or trace.colorscale == "Viridis"

    def test_logz_pre_transforms_z(self):
        grid = pd.DataFrame([[1.0, 10.0], [100.0, 1000.0]], index=[0, 1], columns=[0, 1])
        trace = build_heatmap_trace(grid, value_label="cycles", logz=True, cmap="Viridis")
        np.testing.assert_allclose(np.array(trace.z), np.log10(grid.to_numpy()))

    def test_coloraxis_attaches_to_shared_axis(self):
        grid = pd.DataFrame([[1.0]], index=[0], columns=[0])
        trace = build_heatmap_trace(
            grid, value_label="cycles", logz=False, cmap="Viridis", coloraxis="coloraxis"
        )
        assert trace.coloraxis == "coloraxis"
        # When attached to a shared coloraxis, the per-trace colorscale is not set.
        assert trace.colorscale is None

    def test_explicit_cmin_cmax_passed_through(self):
        grid = pd.DataFrame([[1.0, 2.0]], index=[0], columns=[0, 1])
        trace = build_heatmap_trace(
            grid, value_label="cycles", logz=False, cmap="Viridis", cmin=0.5, cmax=2.5
        )
        assert trace.zmin == 0.5
        assert trace.zmax == 2.5
```

If `tests/python/test_shared_grid.py` doesn't already import `pd`, add `import pandas as pd` at the top of the file.

- [ ] **Step 4.2: Run the tests to verify they fail**

```bash
python3 -m pytest tests/python/test_shared_grid.py::TestBuildHeatmapTrace -v
```

Expected: `ImportError: cannot import name 'build_heatmap_trace'`.

- [ ] **Step 4.3: Implement `build_heatmap_trace`**

Modify `scripts/ferret_plot/kinds/_shared.py`. Add the import at the top:

```python
import plotly.graph_objects as go
```

Append this function to the module (keep `render_heatmap_cell` in place):

```python
def build_heatmap_trace(  # noqa: PLR0913
    grid: pd.DataFrame,
    *,
    value_label: str,
    logz: bool,
    cmap: str,
    cmin: float | None = None,
    cmax: float | None = None,
    coloraxis: str | None = None,
) -> go.Heatmap:
    """Build a `go.Heatmap` trace from a prepared grid.

    `logz=True` pre-transforms z via np.log10 (plotly's colorscale does
    not accept a log norm directly). NaNs render transparent by default;
    callers can paint a grey background on the layout for "missing"
    cells if they want a visible indicator.

    When `coloraxis` is set, the trace attaches to that shared axis and
    its per-trace colorscale/zmin/zmax are left unset — the caller
    configures them on `layout.coloraxis`.
    """
    z = np.log10(grid.to_numpy(dtype=float)) if logz else grid.to_numpy(dtype=float)
    if coloraxis is not None:
        return go.Heatmap(
            x=list(grid.columns),
            y=list(grid.index),
            z=z,
            coloraxis=coloraxis,
            hovertemplate="x=%{x}<br>y=%{y}<br>" + value_label + "=%{z}<extra></extra>",
        )
    return go.Heatmap(
        x=list(grid.columns),
        y=list(grid.index),
        z=z,
        colorscale=cmap,
        zmin=cmin,
        zmax=cmax,
        colorbar=dict(title=value_label),
        hovertemplate="x=%{x}<br>y=%{y}<br>" + value_label + "=%{z}<extra></extra>",
    )
```

- [ ] **Step 4.4: Run the tests to verify they pass**

```bash
python3 -m pytest tests/python/test_shared_grid.py -v
```

Expected: all tests pass. If the `colorscale[0][1]` assertion fails because plotly normalizes a named colorscale into a list of `[stop, color]` tuples on construction, relax the assertion to `assert trace.colorscale is not None`.

- [ ] **Step 4.5: Commit**

```bash
git add scripts/ferret_plot/kinds/_shared.py tests/python/test_shared_grid.py
git commit --no-gpg-sign -m "$(cat <<'EOF'
feat(plot): add build_heatmap_trace for plotly heatmap traces

Shared builder used by the plotly heatmap and facets kinds in
upcoming tasks. Pre-logs z when logz=True; attaches to a shared
coloraxis when one is passed so the facets kind can render N traces
under a single colorbar. The matplotlib render_heatmap_cell stays
in place until heatmap and facets migrate.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 5: Migrate `surface.py` to plotly

**Files:**
- Modify: `scripts/ferret_plot/kinds/surface.py`
- Modify: `scripts/ferret_plot/cli.py` (surface subparser only)
- Modify: `tests/python/test_surface.py`
- Modify: `tests/python/test_cli.py:71-115`

- [ ] **Step 5.1: Add `--cmap` to the surface subparser**

In `scripts/ferret_plot/cli.py`, in the `surface = sub.add_parser(...)` block, add after the `--azim` argument:

```python
    surface.add_argument("--cmap", default="Earth", help="colorscale name (default: Earth, a terrain-style scale)")
```

- [ ] **Step 5.2: Rewrite the surface tests for plotly**

Replace the body of `tests/python/test_surface.py` with:

```python
"""Tests for ferret_plot.kinds.surface (plotly backend)."""

from __future__ import annotations

import numpy as np
import plotly.graph_objects as go
import pytest
from conftest import make_args

from ferret_plot.errors import PlotError
from ferret_plot.kinds import surface as surface_kind
from fixtures import dct_df, tage_capacity_df

_SURFACE_ELEV = 42.0
_SURFACE_AZIM = -35.0
_MISSING_BRANCH_AMOUNT = 128
_MISSING_PATTERN_AMOUNT = 8


def _args(**overrides):
    return make_args("surface", **overrides)


class TestSurfaceMakeFigure:
    def test_returns_plotly_figure_with_surface_trace(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args())
        assert isinstance(fig, go.Figure)
        assert len(fig.data) == 1
        assert fig.data[0].type == "surface"

    def test_default_axes_follow_two_axis_csv_order(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args())
        assert fig.layout.scene.xaxis.title.text == "branch_amount"
        assert fig.layout.scene.yaxis.title.text == "pattern_amount"
        assert "cycles per site" in fig.layout.scene.zaxis.title.text

    def test_explicit_x_y_transpose_axes(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args(x="pattern_amount", y="branch_amount"))
        assert fig.layout.scene.xaxis.title.text == "pattern_amount"
        assert fig.layout.scene.yaxis.title.text == "branch_amount"

    def test_default_colorscale_is_earth(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args())
        # Plotly normalizes named colorscales into a list of [stop, color] tuples.
        # Just assert it's set to something — exact contents are colorscale-specific.
        assert fig.data[0].colorscale is not None

    def test_logz_pre_logs_surfacecolor(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args(logz=True))
        z = fig.data[0].z
        sc = fig.data[0].surfacecolor
        assert sc is not None
        np.testing.assert_allclose(np.array(sc), np.log10(np.array(z)))

    def test_camera_flags_set_view_angles(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args(elev=_SURFACE_ELEV, azim=_SURFACE_AZIM))
        eye = fig.layout.scene.camera.eye
        # Convert back from cartesian to roughly check angles match.
        r = np.sqrt(eye.x**2 + eye.y**2 + eye.z**2)
        elev_deg = np.degrees(np.arcsin(eye.z / r))
        assert abs(elev_deg - _SURFACE_ELEV) < 0.5

    def test_one_axis_csv_raises(self):
        df = dct_df(chain_lengths=(100, 200, 300))
        with pytest.raises(PlotError, match="at least 2 varying axis columns"):
            surface_kind.make_figure(df, _args())

    def test_invalid_x_raises(self):
        df = tage_capacity_df()
        with pytest.raises(PlotError, match="not a column"):
            surface_kind.make_figure(df, _args(x="not_a_column"))

    def test_invalid_y_raises(self):
        df = tage_capacity_df()
        with pytest.raises(PlotError, match="not a column"):
            surface_kind.make_figure(df, _args(y="not_a_column"))

    def test_missing_grid_cell_raises(self):
        df = tage_capacity_df(branch_amounts=(64, 128), pattern_amounts=(4, 8))
        df = df[~((df["branch_amount"] == _MISSING_BRANCH_AMOUNT) & (df["pattern_amount"] == _MISSING_PATTERN_AMOUNT))]
        with pytest.raises(PlotError, match="missing grid cells"):
            surface_kind.make_figure(df, _args())

    def test_logz_rejects_non_positive_values(self):
        df = tage_capacity_df()
        df.loc[df.index[0], "cycles_per_site_min"] = 0.0
        with pytest.raises(PlotError, match="--logz requires positive"):
            surface_kind.make_figure(df, _args(logz=True))

    def test_invalid_cmap_raises(self):
        df = tage_capacity_df()
        with pytest.raises(PlotError, match="not a valid colorscale"):
            surface_kind.make_figure(df, _args(cmap="not_a_real_colorscale"))
```

- [ ] **Step 5.3: Run the tests to verify they fail**

```bash
python3 -m pytest tests/python/test_surface.py -v
```

Expected: all tests fail with import errors or assertion errors because surface.py is still matplotlib.

- [ ] **Step 5.4: Rewrite `kinds/surface.py` for plotly**

Replace the contents of `scripts/ferret_plot/kinds/surface.py` with:

```python
"""3D surface renderer (plotly backend) for CSVs with at least 2 varying axis columns."""

from __future__ import annotations

import argparse
import math

import numpy as np
import pandas as pd
import plotly.colors
import plotly.graph_objects as go

from ferret_plot.columns import bench_name, resolve_metric
from ferret_plot.errors import PlotError
from ferret_plot.formatting import decimate_indices, human_readable
from ferret_plot.kinds._shared import prepare_grid, resolve_heatmap_xy
from ferret_plot.registry import resolve_defaults

_DEFAULT_CMAP = "Earth"


def _z_values(grid: pd.DataFrame) -> np.ndarray:
    try:
        return grid.to_numpy(dtype=float)
    except (TypeError, ValueError) as e:
        raise PlotError("surface plot metric values must be numeric") from e


def _validate_cmap(cmap: str) -> str:
    if cmap not in plotly.colors.named_colorscales():
        raise PlotError(
            f"--cmap={cmap!r} is not a valid colorscale; "
            f"valid names: {sorted(plotly.colors.named_colorscales())[:8]}..."
        )
    return cmap


def _axis_ticks(labels: list[object]) -> tuple[list[int], list[str]]:
    kept = decimate_indices(labels)
    return kept, [human_readable(labels[i]) for i in kept]


def _camera_eye(elev_deg: float, azim_deg: float, r: float = 1.8) -> dict[str, float]:
    """Convert (elev, azim) degrees to a cartesian camera.eye for plotly.

    elev is the angle above the XY plane; azim is the angle around the Z axis.
    """
    elev = math.radians(elev_deg)
    azim = math.radians(azim_deg)
    return {
        "x": r * math.cos(elev) * math.cos(azim),
        "y": r * math.cos(elev) * math.sin(azim),
        "z": r * math.sin(elev),
    }


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> go.Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    xcol, ycol = resolve_heatmap_xy(df, args, defaults)
    grid = prepare_grid(df, xcol=xcol, ycol=ycol, value_col=metric.column, require_complete=True)
    z = _z_values(grid)

    cmap = _validate_cmap(args.cmap or _DEFAULT_CMAP)

    surfacecolor = None
    cmin: float | None = None
    cmax: float | None = None
    if args.logz:
        if np.nanmin(z) <= 0:
            raise PlotError("--logz requires positive metric values")
        surfacecolor = np.log10(z)
        cmin = float(np.nanmin(surfacecolor))
        cmax = float(np.nanmax(surfacecolor))
    else:
        cmin = float(np.nanmin(z))
        cmax = float(np.nanmax(z))

    x_positions = np.arange(len(grid.columns))
    y_positions = np.arange(len(grid.index))

    surface = go.Surface(
        x=x_positions,
        y=y_positions,
        z=z,
        surfacecolor=surfacecolor,
        colorscale=cmap,
        cmin=cmin,
        cmax=cmax,
        colorbar=dict(title=metric.label),
        lighting=dict(ambient=0.6, diffuse=0.8, specular=0.05, roughness=0.5, fresnel=0.2),
        hovertemplate=(
            f"{xcol}=%{{x}}<br>{ycol}=%{{y}}<br>{metric.label}=%{{z}}<extra></extra>"
        ),
    )

    x_tickvals, x_ticktext = _axis_ticks(list(grid.columns))
    y_tickvals, y_ticktext = _axis_ticks(list(grid.index))

    fig = go.Figure(data=[surface])
    fig.update_layout(
        title=f"{bench_name(df)}: {metric.label} surface ({ycol} × {xcol})",
        scene=dict(
            xaxis=dict(
                title=xcol,
                tickvals=x_tickvals,
                ticktext=x_ticktext,
            ),
            yaxis=dict(
                title=ycol,
                tickvals=y_tickvals,
                ticktext=y_ticktext,
            ),
            zaxis=dict(title=metric.label),
            aspectratio=dict(
                x=max(len(grid.columns), 1) * 1.25,
                y=max(len(grid.index), 1) * 1.5,
                z=max(len(grid.columns), len(grid.index), 1) * 0.95,
            ),
            camera=dict(eye=_camera_eye(args.elev, args.azim)),
        ),
        margin=dict(l=10, r=10, t=60, b=10),
    )
    return fig
```

- [ ] **Step 5.5: Run the surface tests to verify they pass**

```bash
python3 -m pytest tests/python/test_surface.py -v
```

Expected: all tests pass. If `test_camera_flags_set_view_angles` fails because the azim sign convention differs between matplotlib and plotly, adjust `_camera_eye` (likely negate `azim` or `y`).

- [ ] **Step 5.6: Update the CLI-level surface tests**

In `tests/python/test_cli.py`, in `class TestSurfaceSubcommand`, the existing `test_surface_parser_defaults` test asserts `_SURFACE_ELEV_DEFAULT = 30.0` and `_SURFACE_AZIM_DEFAULT = -60.0`, but the cli currently uses `elev=20.0, azim=-13.0`. Verify what the CLI defaults actually are now (check `cli.py:69-70`). Keep them as-is — they're chosen for the today look. If the test reads stale constants, fix the constants in the test file to match the CLI:

Open `tests/python/test_cli.py` and update lines 13-14:

```python
_SURFACE_ELEV_DEFAULT = 20.0
_SURFACE_AZIM_DEFAULT = -13.0
```

(These match `cli.py:69-70`. If they don't match the current CLI, update the constants here to match what's in `cli.py`.)

For the `test_surface_produces_png` test (lines 81-98), this hits the kaleido image path which requires Chrome. Monkeypatch `output._chrome_available` and `fig.write_image` so the test doesn't actually launch a browser. Replace the test with:

```python
    def test_surface_produces_png(self, tmp_path, monkeypatch):
        from ferret_plot import output

        csv_path = _write_csv(tage_capacity_df(), tmp_path, "tage.csv")
        out_path = tmp_path / "surface.png"

        captured = {}

        def fake_write_image(self, path, **kwargs):
            captured["path"] = path
            captured["kwargs"] = kwargs
            with open(path, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\n" + b"\x00" * 16)

        monkeypatch.setattr(output, "_chrome_available", lambda: True)
        output._chrome_probe_cache.clear()
        monkeypatch.setattr("plotly.graph_objects.Figure.write_image", fake_write_image)

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
        assert captured["kwargs"]["format"] == "png"
```

- [ ] **Step 5.7: Run the full test suite**

```bash
./scripts/test_py.sh
```

Expected: all tests pass. Non-migrated kinds (line, heatmap, facets) still pass because their tests are unchanged and `output.emit` still falls through to `savefig` for matplotlib figures.

- [ ] **Step 5.8: Smoke-test surface plot output**

Run, from the repo root:

```bash
python3 -c "
import pandas as pd
from tests.python.fixtures import tage_capacity_df
df = tage_capacity_df()
df.to_csv('/tmp/tage.csv', index=False)
"
python3 scripts/plot.py surface /tmp/tage.csv --x=branch_amount --y=pattern_amount --out=/tmp/surface.html
```

Expected: exit code 0; `/tmp/surface.html` exists and contains `plotly` in its content. Open it in a browser and confirm the surface renders with smooth coloring (per-pixel gradient across faces, not the matplotlib quad-flat look). Test interactivity: rotate the plot, hover a point and see the (branch_amount, pattern_amount, cycles) tooltip.

Also try a PNG:

```bash
python3 scripts/plot.py surface /tmp/tage.csv --x=branch_amount --y=pattern_amount --out=/tmp/surface.png
```

Expected: a PNG file is created and is non-empty. (Requires Chrome on PATH; if missing, the CLI prints the install-hint PlotError.)

- [ ] **Step 5.9: Commit**

```bash
git add scripts/ferret_plot/cli.py scripts/ferret_plot/kinds/surface.py tests/python/test_surface.py tests/python/test_cli.py
git commit --no-gpg-sign -m "$(cat <<'EOF'
feat(plot): migrate surface kind to plotly

3D surface now renders via go.Surface with per-vertex Gouraud color
interpolation (smooth terrain shading vs matplotlib's per-quad flat
shading). Adds --cmap=NAME with default Earth, a terrain-style scale.
HTML output is interactive (rotate, zoom, hover); PNG/SVG/PDF go
through kaleido. Other kinds still on matplotlib; they migrate next.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 6: Migrate `heatmap.py` to plotly

**Files:**
- Modify: `scripts/ferret_plot/cli.py` (heatmap subparser)
- Modify: `scripts/ferret_plot/kinds/heatmap.py`
- Modify: `tests/python/test_heatmap.py`
- Modify: `tests/python/test_cli.py` (heatmap subcommand tests)

- [ ] **Step 6.1: Add `--cmap` to the heatmap subparser**

In `scripts/ferret_plot/cli.py`, in the `heat = sub.add_parser(...)` block, add after `--logz`:

```python
    heat.add_argument("--cmap", default="Viridis", help="colorscale name (default: Viridis)")
```

- [ ] **Step 6.2: Rewrite the heatmap tests for plotly**

Open `tests/python/test_heatmap.py`. Replace matplotlib axis assertions (`ax.get_xlabel()`, `ax.images[0]`, etc.) with plotly figure-dict assertions:

```python
"""Tests for ferret_plot.kinds.heatmap (plotly backend)."""

from __future__ import annotations

import numpy as np
import plotly.graph_objects as go
import pytest
from conftest import make_args

from ferret_plot.errors import PlotError
from ferret_plot.kinds import heatmap as heatmap_kind
from fixtures import dbf_df, dct_df


def _args(**overrides):
    return make_args("heatmap", **overrides)


class TestHeatmapMakeFigure:
    def test_returns_plotly_figure_with_heatmap_trace(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args())
        assert isinstance(fig, go.Figure)
        assert len(fig.data) == 1
        assert fig.data[0].type == "heatmap"

    def test_axis_titles_match_resolved_xy(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args())
        # dbf_df has branches × spacing_bytes; resolver picks branches as x.
        assert fig.layout.xaxis.title.text == "branches"
        assert fig.layout.yaxis.title.text == "spacing_bytes"

    def test_explicit_x_y_transpose(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args(x="spacing_bytes", y="branches"))
        assert fig.layout.xaxis.title.text == "spacing_bytes"
        assert fig.layout.yaxis.title.text == "branches"

    def test_default_colorscale_is_viridis(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args())
        assert fig.data[0].colorscale is not None

    def test_logz_pre_logs_z(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args(logz=True))
        z = np.array(fig.data[0].z)
        # All values in dbf_df are positive ns/cycles, so log10 is finite.
        assert np.isfinite(z).all()

    def test_one_axis_csv_raises(self):
        df = dct_df(chain_lengths=(100, 200, 300))
        with pytest.raises(PlotError, match="at least 2 varying axis columns"):
            heatmap_kind.make_figure(df, _args())

    def test_invalid_x_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError, match="not a column"):
            heatmap_kind.make_figure(df, _args(x="not_a_column"))

    def test_invalid_cmap_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError, match="not a valid colorscale"):
            heatmap_kind.make_figure(df, _args(cmap="not_a_real_colorscale"))
```

- [ ] **Step 6.3: Run heatmap tests to verify they fail**

```bash
python3 -m pytest tests/python/test_heatmap.py -v
```

Expected: failures because heatmap.py is still matplotlib.

- [ ] **Step 6.4: Rewrite `kinds/heatmap.py` for plotly**

Replace the contents of `scripts/ferret_plot/kinds/heatmap.py` with:

```python
"""2D heatmap renderer (plotly backend) for CSVs with at least 2 varying axis columns."""

from __future__ import annotations

import argparse

import numpy as np
import pandas as pd
import plotly.colors
import plotly.graph_objects as go

from ferret_plot.columns import bench_name, resolve_metric
from ferret_plot.errors import PlotError
from ferret_plot.formatting import decimate_indices, human_readable
from ferret_plot.kinds._shared import build_heatmap_trace, prepare_grid, resolve_heatmap_xy
from ferret_plot.registry import resolve_defaults

_DEFAULT_CMAP = "Viridis"


def _validate_cmap(cmap: str) -> str:
    if cmap not in plotly.colors.named_colorscales():
        raise PlotError(f"--cmap={cmap!r} is not a valid colorscale")
    return cmap


def _axis_ticks(labels: list[object]) -> tuple[list[int], list[str]]:
    kept = decimate_indices(labels)
    return [labels[i] for i in kept], [human_readable(labels[i]) for i in kept]


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> go.Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    xcol, ycol = resolve_heatmap_xy(df, args, defaults)
    grid = prepare_grid(df, xcol=xcol, ycol=ycol, value_col=metric.column)

    cmap = _validate_cmap(args.cmap or _DEFAULT_CMAP)

    if args.logz:
        z_all = grid.to_numpy(dtype=float)
        if np.nanmin(z_all) <= 0:
            raise PlotError("--logz requires positive metric values")

    trace = build_heatmap_trace(
        grid,
        value_label=metric.label,
        logz=args.logz,
        cmap=cmap,
    )

    x_tickvals, x_ticktext = _axis_ticks(list(grid.columns))
    y_tickvals, y_ticktext = _axis_ticks(list(grid.index))

    fig = go.Figure(data=[trace])
    fig.update_layout(
        title=f"{bench_name(df)}: {metric.label} ({ycol} × {xcol})",
        xaxis=dict(title=xcol, tickvals=x_tickvals, ticktext=x_ticktext),
        yaxis=dict(title=ycol, tickvals=y_tickvals, ticktext=y_ticktext),
        # NaN cells render transparent in plotly. A lightgrey plot
        # background gives them today's "missing" look.
        plot_bgcolor="lightgrey",
        margin=dict(l=60, r=20, t=60, b=60),
    )
    return fig
```

- [ ] **Step 6.5: Run heatmap tests to verify they pass**

```bash
python3 -m pytest tests/python/test_heatmap.py -v
```

Expected: all tests pass.

- [ ] **Step 6.6: Update the CLI heatmap test for kaleido mocking**

In `tests/python/test_cli.py`, replace `class TestHeatmapSubcommand` with the kaleido-mocked version (mirrors the surface pattern from Step 5.6):

```python
class TestHeatmapSubcommand:
    def test_heatmap_produces_png(self, tmp_path, monkeypatch):
        from ferret_plot import output

        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "heat.png"

        def fake_write_image(self, path, **kwargs):
            with open(path, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\n" + b"\x00" * 16)

        monkeypatch.setattr(output, "_chrome_available", lambda: True)
        output._chrome_probe_cache.clear()
        monkeypatch.setattr("plotly.graph_objects.Figure.write_image", fake_write_image)

        rc = cli.main(["heatmap", csv_path, "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()
        assert out_path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"

    def test_heatmap_with_explicit_xy(self, tmp_path, monkeypatch):
        from ferret_plot import output

        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "heat-xy.png"

        def fake_write_image(self, path, **kwargs):
            with open(path, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\n" + b"\x00" * 16)

        monkeypatch.setattr(output, "_chrome_available", lambda: True)
        output._chrome_probe_cache.clear()
        monkeypatch.setattr("plotly.graph_objects.Figure.write_image", fake_write_image)

        rc = cli.main(
            ["heatmap", csv_path, "--x", "spacing_bytes", "--y", "branches", "--out", str(out_path)]
        )
        assert rc == 0
        assert out_path.exists()
```

- [ ] **Step 6.7: Run the full test suite**

```bash
./scripts/test_py.sh
```

Expected: all tests pass.

- [ ] **Step 6.8: Commit**

```bash
git add scripts/ferret_plot/cli.py scripts/ferret_plot/kinds/heatmap.py tests/python/test_heatmap.py tests/python/test_cli.py
git commit --no-gpg-sign -m "$(cat <<'EOF'
feat(plot): migrate heatmap kind to plotly

heatmap returns a plotly Figure built from build_heatmap_trace.
Adds --cmap (default Viridis). HTML output gets exact-value hover
tooltips at each cell, a usability win the matplotlib version
couldn't offer.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 7: Migrate `facets.py` to plotly

**Files:**
- Modify: `scripts/ferret_plot/cli.py` (facets subparser)
- Modify: `scripts/ferret_plot/kinds/facets.py`
- Modify: `tests/python/test_facets.py`
- Modify: `tests/python/test_cli.py` (facets subcommand test)

- [ ] **Step 7.1: Add `--cmap` to the facets subparser**

In `scripts/ferret_plot/cli.py`, in the `fac = sub.add_parser(...)` block, add after `--logz`:

```python
    fac.add_argument("--cmap", default="Viridis", help="colorscale name (default: Viridis)")
```

- [ ] **Step 7.2: Rewrite facets tests for plotly**

Replace the body of `tests/python/test_facets.py`:

```python
"""Tests for ferret_plot.kinds.facets (plotly backend)."""

from __future__ import annotations

import plotly.graph_objects as go
import pytest
from conftest import make_args

from ferret_plot.errors import PlotError
from ferret_plot.kinds import facets as facets_kind
from fixtures import dbf_df, three_axis_df


def _args(**overrides):
    return make_args("facets", **overrides)


class TestFacetsMakeFigure:
    def test_returns_plotly_figure(self):
        df = three_axis_df(variants=("a", "b", "c"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        assert isinstance(fig, go.Figure)

    def test_one_heatmap_trace_per_facet_value(self):
        df = three_axis_df(variants=("a", "b", "c"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        assert len(fig.data) == 3
        for trace in fig.data:
            assert trace.type == "heatmap"

    def test_shared_coloraxis_used(self):
        df = three_axis_df(variants=("a", "b", "c"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        for trace in fig.data:
            assert trace.coloraxis == "coloraxis"
        assert fig.layout.coloraxis.cmin is not None
        assert fig.layout.coloraxis.cmax is not None
        # cmax must be > cmin for a meaningful color range.
        assert fig.layout.coloraxis.cmax > fig.layout.coloraxis.cmin

    def test_missing_facet_raises(self):
        df = dbf_df()  # 2-axis, no facet column
        with pytest.raises(PlotError, match="--facet=COL is required"):
            facets_kind.make_figure(df, _args())

    def test_facet_equals_x_raises(self):
        df = three_axis_df(variants=("a", "b"))
        with pytest.raises(PlotError, match="same as --facet"):
            facets_kind.make_figure(df, _args(facet="variant", x="variant"))

    def test_invalid_cmap_raises(self):
        df = three_axis_df(variants=("a", "b"))
        with pytest.raises(PlotError, match="not a valid colorscale"):
            facets_kind.make_figure(df, _args(facet="variant", cmap="not_a_real_colorscale"))
```

- [ ] **Step 7.3: Run facets tests to verify they fail**

```bash
python3 -m pytest tests/python/test_facets.py -v
```

Expected: failures.

- [ ] **Step 7.4: Rewrite `kinds/facets.py` for plotly**

Replace the contents of `scripts/ferret_plot/kinds/facets.py`:

```python
"""Grid-of-heatmaps renderer (plotly backend) for CSVs with at least 3 varying axes."""

from __future__ import annotations

import argparse
import math

import numpy as np
import pandas as pd
import plotly.colors
import plotly.graph_objects as go
from plotly.subplots import make_subplots

from ferret_plot.columns import bench_name, resolve_metric
from ferret_plot.errors import PlotError
from ferret_plot.formatting import decimate_indices, human_readable
from ferret_plot.kinds._shared import build_heatmap_trace, prepare_grid, resolve_heatmap_xy
from ferret_plot.registry import BenchmarkDefaults, resolve_defaults

_DEFAULT_CMAP = "Viridis"


def _resolve_facet(df: pd.DataFrame, args: argparse.Namespace, defaults: BenchmarkDefaults) -> str:
    if args.facet is not None:
        if args.facet not in df.columns:
            raise PlotError(f"--facet={args.facet!r} is not a column in the CSV")
        return args.facet
    if defaults.facet_col is not None and defaults.facet_col in df.columns:
        return defaults.facet_col
    raise PlotError("--facet=COL is required (and no registry default applies)")


def _validate_cmap(cmap: str) -> str:
    if cmap not in plotly.colors.named_colorscales():
        raise PlotError(f"--cmap={cmap!r} is not a valid colorscale")
    return cmap


def _axis_ticks(labels: list[object]) -> tuple[list[object], list[str]]:
    kept = decimate_indices(labels)
    return [labels[i] for i in kept], [human_readable(labels[i]) for i in kept]


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> go.Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    facet = _resolve_facet(df, args, defaults)
    if args.x == facet:
        raise PlotError(f"--x={args.x!r} is the same as --facet; pick a different axis")
    if args.y == facet:
        raise PlotError(f"--y={args.y!r} is the same as --facet; pick a different axis")
    xcol, ycol = resolve_heatmap_xy(df, args, defaults, exclude=frozenset({facet}))

    facet_values = df[facet].dropna().unique().tolist()
    if not facet_values:
        raise PlotError(f"--facet={facet!r} has no non-NaN values to plot")
    try:
        facet_values = sorted(facet_values)
    except TypeError:
        facet_values = sorted(facet_values, key=str)
    n = len(facet_values)
    ncols = max(1, math.ceil(math.sqrt(n)))
    nrows = math.ceil(n / ncols)

    cmap = _validate_cmap(args.cmap or _DEFAULT_CMAP)

    metric_values = df[metric.column].to_numpy()
    if args.logz:
        if np.nanmin(metric_values) <= 0:
            raise PlotError("--logz requires positive metric values")
        cmin = float(np.log10(np.nanmin(metric_values)))
        cmax = float(np.log10(np.nanmax(metric_values)))
    else:
        cmin = float(np.nanmin(metric_values))
        cmax = float(np.nanmax(metric_values))

    fig = make_subplots(
        rows=nrows,
        cols=ncols,
        subplot_titles=[f"{facet}={v}" for v in facet_values],
    )

    for idx, value in enumerate(facet_values):
        sub_df = df[df[facet] == value]
        grid = prepare_grid(sub_df, xcol=xcol, ycol=ycol, value_col=metric.column)
        trace = build_heatmap_trace(
            grid,
            value_label=metric.label,
            logz=args.logz,
            cmap=cmap,
            coloraxis="coloraxis",
        )
        row = idx // ncols + 1
        col = idx % ncols + 1
        fig.add_trace(trace, row=row, col=col)

        x_tickvals, x_ticktext = _axis_ticks(list(grid.columns))
        y_tickvals, y_ticktext = _axis_ticks(list(grid.index))
        fig.update_xaxes(
            title_text=xcol,
            tickvals=x_tickvals,
            ticktext=x_ticktext,
            row=row,
            col=col,
        )
        fig.update_yaxes(
            title_text=ycol,
            tickvals=y_tickvals,
            ticktext=y_ticktext,
            row=row,
            col=col,
        )

    fig.update_layout(
        title=f"{bench_name(df)}: {metric.label}",
        coloraxis=dict(
            colorscale=cmap,
            cmin=cmin,
            cmax=cmax,
            colorbar=dict(title=metric.label),
        ),
        # NaN cells render transparent in plotly; match today's grey
        # "missing" indication via a lightgrey plot background.
        plot_bgcolor="lightgrey",
        margin=dict(l=60, r=20, t=80, b=60),
    )
    return fig
```

- [ ] **Step 7.5: Run facets tests to verify they pass**

```bash
python3 -m pytest tests/python/test_facets.py -v
```

Expected: all tests pass.

- [ ] **Step 7.6: Update the CLI facets test for kaleido mocking**

Replace `class TestFacetsSubcommand` in `tests/python/test_cli.py`:

```python
class TestFacetsSubcommand:
    def test_facets_produces_png(self, tmp_path, monkeypatch):
        from ferret_plot import output

        csv_path = _write_csv(three_axis_df(variants=("a", "b", "c")), tmp_path, "3ax.csv")
        out_path = tmp_path / "facets.png"

        def fake_write_image(self, path, **kwargs):
            with open(path, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\n" + b"\x00" * 16)

        monkeypatch.setattr(output, "_chrome_available", lambda: True)
        output._chrome_probe_cache.clear()
        monkeypatch.setattr("plotly.graph_objects.Figure.write_image", fake_write_image)

        rc = cli.main(["facets", csv_path, "--facet", "variant", "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()
        assert out_path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
```

- [ ] **Step 7.7: Run the full suite**

```bash
./scripts/test_py.sh
```

Expected: all tests pass.

- [ ] **Step 7.8: Commit**

```bash
git add scripts/ferret_plot/cli.py scripts/ferret_plot/kinds/facets.py tests/python/test_facets.py tests/python/test_cli.py
git commit --no-gpg-sign -m "$(cat <<'EOF'
feat(plot): migrate facets kind to plotly

facets returns a plotly subplot grid where every heatmap trace
shares a single coloraxis so one colorbar covers the figure. Adds
--cmap (default Viridis).

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 8: Migrate `line.py` to plotly

**Files:**
- Modify: `scripts/ferret_plot/kinds/line.py`
- Modify: `tests/python/test_line.py`
- Modify: `tests/python/test_cli.py` (line subcommand tests)

- [ ] **Step 8.1: Rewrite line tests for plotly**

Replace the body of `tests/python/test_line.py`:

```python
"""Tests for ferret_plot.kinds.line (plotly backend)."""

from __future__ import annotations

import plotly.graph_objects as go
import pytest
from conftest import make_args

from ferret_plot.errors import PlotError
from ferret_plot.kinds import line as line_kind
from fixtures import dbf_df, dct_df

_LARGE_POINTS = 6000


def _args(**overrides):
    return make_args("line", **overrides)


class TestLineMakeFigure:
    def test_returns_plotly_figure(self):
        df = dct_df()
        fig = line_kind.make_figure(df, _args())
        assert isinstance(fig, go.Figure)

    def test_single_series_one_trace(self):
        df = dct_df(chain_lengths=(100, 200, 400))
        fig = line_kind.make_figure(df, _args())
        assert len(fig.data) == 1
        assert fig.data[0].type in ("scatter", "scattergl")

    def test_multi_series_one_trace_per_group(self):
        df = dbf_df(branches=(1, 2), spacing=(16, 32))
        fig = line_kind.make_figure(df, _args(x="branches"))
        # 2 spacings × 1 = 2 traces.
        assert len(fig.data) == 2
        names = {t.name for t in fig.data}
        assert any("spacing_bytes=16" in n for n in names)
        assert any("spacing_bytes=32" in n for n in names)

    def test_log_scale_when_xscale_log(self):
        df = dbf_df(branches=(1, 2, 4, 8), spacing=(16,))
        fig = line_kind.make_figure(df, _args(x="branches", xscale="log"))
        assert fig.layout.xaxis.type == "log"

    def test_linear_scale_when_xscale_linear(self):
        df = dct_df(chain_lengths=(1, 2, 3, 4))
        fig = line_kind.make_figure(df, _args(xscale="linear"))
        assert fig.layout.xaxis.type == "linear"

    def test_ymax_sets_y_range(self):
        df = dct_df()
        fig = line_kind.make_figure(df, _args(ymax=2.5))
        assert list(fig.layout.yaxis.range) == [0, 2.5]

    def test_invalid_x_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError, match="not a column"):
            line_kind.make_figure(df, _args(x="not_a_column"))

    def test_scattergl_used_for_large_point_count(self):
        # Synthesize > 5000 total points across one series.
        df = dct_df(chain_lengths=tuple(range(_LARGE_POINTS + 100)))
        fig = line_kind.make_figure(df, _args())
        assert fig.data[0].type == "scattergl"
```

- [ ] **Step 8.2: Run line tests to verify they fail**

```bash
python3 -m pytest tests/python/test_line.py -v
```

Expected: failures.

- [ ] **Step 8.3: Rewrite `kinds/line.py` for plotly**

Replace the contents of `scripts/ferret_plot/kinds/line.py`:

```python
"""Line+series plot (plotly backend)."""

from __future__ import annotations

import argparse

import pandas as pd
import plotly.graph_objects as go

from ferret_plot.columns import bench_name, resolve_metric, varying_axis_columns
from ferret_plot.errors import PlotError
from ferret_plot.formatting import decimate_indices, human_readable
from ferret_plot.registry import BenchmarkDefaults, resolve_defaults

_SCATTERGL_POINT_THRESHOLD = 5000


def _resolve_x(df: pd.DataFrame, args: argparse.Namespace, defaults: BenchmarkDefaults) -> str:
    if args.x is not None:
        if args.x not in df.columns:
            raise PlotError(f"--x={args.x!r} is not a column in the CSV")
        return args.x
    if defaults.line_x is not None and defaults.line_x in df.columns:
        return defaults.line_x
    cols = varying_axis_columns(df)
    if not cols:
        raise PlotError("no varying axis column to use as X")
    return cols[0]


def _resolve_series(df: pd.DataFrame, args: argparse.Namespace, xcol: str) -> list[str]:
    if args.series is not None:
        cols = [c.strip() for c in args.series.split(",") if c.strip()]
        for c in cols:
            if c not in df.columns:
                raise PlotError(f"--series column {c!r} is not a column in the CSV")
            if c == xcol:
                raise PlotError(f"--series column {c!r} is the same as the X column")
        return cols
    return [c for c in varying_axis_columns(df) if c != xcol]


def _trace_cls(total_points: int):
    return go.Scattergl if total_points > _SCATTERGL_POINT_THRESHOLD else go.Scatter


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> go.Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    xcol = _resolve_x(df, args, defaults)
    series_cols = _resolve_series(df, args, xcol)
    xscale = args.xscale or defaults.line_xscale or "log"

    total_points = len(df)
    cls = _trace_cls(total_points)

    traces: list[go.BaseTraceType] = []
    if series_cols:
        for keys, sub in df.groupby(series_cols):
            label_keys = keys if isinstance(keys, tuple) else (keys,)
            label = ", ".join(f"{c}={v}" for c, v in zip(series_cols, label_keys, strict=True))
            traces.append(
                cls(
                    x=sub[xcol],
                    y=sub[metric.column],
                    mode="lines+markers",
                    marker=dict(size=4),
                    line=dict(width=1.5),
                    name=label,
                )
            )
    else:
        traces.append(
            cls(
                x=df[xcol],
                y=df[metric.column],
                mode="lines+markers",
                marker=dict(size=4),
                line=dict(width=1.5),
            )
        )

    unique_x = sorted(df[xcol].dropna().unique())
    kept = decimate_indices(unique_x)
    tickvals = [unique_x[i] for i in kept]
    ticktext = [human_readable(unique_x[i]) for i in kept]

    fig = go.Figure(data=traces)
    fig.update_layout(
        title=f"{bench_name(df)}: {metric.label} vs {xcol}",
        xaxis=dict(
            title=xcol,
            type=("log" if xscale == "log" else "linear"),
            tickvals=tickvals,
            ticktext=ticktext,
        ),
        yaxis=dict(
            title=metric.label,
            range=[0, args.ymax] if args.ymax is not None else None,
            rangemode="tozero",
        ),
        showlegend=bool(series_cols),
        margin=dict(l=60, r=20, t=60, b=60),
    )
    return fig
```

- [ ] **Step 8.4: Run line tests to verify they pass**

```bash
python3 -m pytest tests/python/test_line.py -v
```

Expected: all tests pass. If `test_scattergl_used_for_large_point_count` fails because plotly returns `type="scatter"` for `go.Scattergl` traces in some versions, change the assertion to `assert isinstance(fig.data[0], (go.Scattergl,))` and import accordingly.

- [ ] **Step 8.5: Update CLI line tests**

In `tests/python/test_cli.py`, replace `class TestLineSubcommand`:

```python
class TestLineSubcommand:
    def test_line_produces_png(self, tmp_path, monkeypatch):
        from ferret_plot import output

        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "out.png"

        def fake_write_image(self, path, **kwargs):
            with open(path, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\n" + b"\x00" * 16)

        monkeypatch.setattr(output, "_chrome_available", lambda: True)
        output._chrome_probe_cache.clear()
        monkeypatch.setattr("plotly.graph_objects.Figure.write_image", fake_write_image)

        rc = cli.main(["line", csv_path, "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()
        assert out_path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"

    def test_line_with_spacing_bytes_x(self, tmp_path, monkeypatch):
        from ferret_plot import output

        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "out.png"

        def fake_write_image(self, path, **kwargs):
            with open(path, "wb") as f:
                f.write(b"\x89PNG\r\n\x1a\n" + b"\x00" * 16)

        monkeypatch.setattr(output, "_chrome_available", lambda: True)
        output._chrome_probe_cache.clear()
        monkeypatch.setattr("plotly.graph_objects.Figure.write_image", fake_write_image)

        rc = cli.main(["line", csv_path, "--x", "spacing_bytes", "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()
```

Also add an HTML output test under `TestLineSubcommand`:

```python
    def test_line_html_output(self, tmp_path):
        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "out.html"
        rc = cli.main(["line", csv_path, "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()
        body = out_path.read_text()
        assert "plotly" in body.lower()
```

- [ ] **Step 8.6: Run the full suite**

```bash
./scripts/test_py.sh
```

Expected: all tests pass.

- [ ] **Step 8.7: Commit**

```bash
git add scripts/ferret_plot/kinds/line.py tests/python/test_line.py tests/python/test_cli.py
git commit --no-gpg-sign -m "$(cat <<'EOF'
feat(plot): migrate line kind to plotly

line returns a plotly Figure with go.Scatter / go.Scattergl traces.
Hover tooltips now show series label and exact (x, y) values in
HTML output, the original ask behind the migration. Switches to
scattergl above 5000 total points to stay snappy.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 9: Remove matplotlib

**Files:**
- Modify: `requirements.txt`
- Modify: `flake.nix`
- Modify: `scripts/ferret_plot/output.py`
- Modify: `scripts/ferret_plot/formatting.py`
- Modify: `scripts/ferret_plot/kinds/_shared.py`
- Modify: `tests/python/conftest.py`
- Modify: `tests/python/test_output.py`

- [ ] **Step 9.1: Drop matplotlib from `requirements.txt`**

Replace its contents with:

```
# Runtime deps for scripts/ — Nix users: see flake.nix.
numpy
pandas
plotly>=6.1.1
kaleido>=1.0
```

- [ ] **Step 9.2: Drop matplotlib from `flake.nix`**

In the `python3.withPackages` block, remove the `matplotlib` line. The list should look like:

```nix
            (pkgs.python3.withPackages (
              ps:
                with ps; [
                  numpy
                  pandas
                  plotly
                  kaleido
                  pytest
                ]
            ))
```

- [ ] **Step 9.3: Simplify `output.emit` (drop matplotlib passthrough)**

In `scripts/ferret_plot/output.py`, delete the matplotlib branch and helpers:

- Remove the `if _is_mpl_figure(fig): return _emit_mpl(...)` block from the top of `emit`.
- Remove the `_is_mpl_figure` and `_emit_mpl` helper functions.

- [ ] **Step 9.4: Remove `apply_axis` from `formatting.py`**

Open `scripts/ferret_plot/formatting.py` and delete the `apply_axis` function (lines roughly 86-108). Also remove the `import matplotlib.ticker as mticker` and `from matplotlib.axis import Axis` lines at the top. `decimate_indices` and `human_readable` stay.

- [ ] **Step 9.5: Remove `render_heatmap_cell` from `_shared.py`**

Open `scripts/ferret_plot/kinds/_shared.py` and delete the `render_heatmap_cell` function (lines roughly 99-129). Also remove the matplotlib imports at the top:

```python
from matplotlib.axes import Axes
from matplotlib.colors import Normalize
from matplotlib.image import AxesImage
```

`build_heatmap_trace`, `resolve_heatmap_xy`, and `prepare_grid` stay.

- [ ] **Step 9.6: Clean up `conftest.py`**

In `tests/python/conftest.py`, remove these matplotlib bits:

```python
import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt
```

And remove the `_close_mpl_figures` fixture (the entire `@pytest.fixture(autouse=True) def _close_mpl_figures()` block).

After cleanup, the file should still:
- Set up the `scripts/` sys.path.
- Define `_COMMON_DEFAULTS`, `_KIND_DEFAULTS`, and `make_args`.

- [ ] **Step 9.7: Drop the matplotlib passthrough tests**

In `tests/python/test_output.py`, delete `class TestMatplotlibPassthrough` (the matplotlib-figure savefig tests added in Step 3.2). They're no longer applicable.

- [ ] **Step 9.8: Run the full suite**

```bash
./scripts/test_py.sh
```

Expected: all tests pass. If anything still imports matplotlib, find and remove it:

```bash
grep -rn "matplotlib" scripts/ tests/python/
```

Should return only matches inside `*.md` design docs (and zero matches in Python source).

- [ ] **Step 9.9: Smoke-test all four kinds end-to-end**

```bash
python3 -c "
import pandas as pd
from tests.python.fixtures import dbf_df, tage_capacity_df, three_axis_df
dbf_df().to_csv('/tmp/btb.csv', index=False)
tage_capacity_df().to_csv('/tmp/tage.csv', index=False)
three_axis_df(variants=('a','b','c')).to_csv('/tmp/3ax.csv', index=False)
"

python3 scripts/plot.py line    /tmp/btb.csv  --out=/tmp/line.html
python3 scripts/plot.py heatmap /tmp/btb.csv  --out=/tmp/heat.html
python3 scripts/plot.py surface /tmp/tage.csv --x=branch_amount --y=pattern_amount --out=/tmp/surface.html
python3 scripts/plot.py facets  /tmp/3ax.csv  --facet=variant --out=/tmp/facets.html
```

Expected: all four exit 0; each HTML opens in a browser and renders correctly with hover tooltips. Verify visually:
- `line.html` — hover any point, see exact (x, y) value.
- `heat.html` — hover any cell, see exact value.
- `surface.html` — smooth color gradient across faces, hover/rotate works.
- `facets.html` — single shared colorbar across all subplots.

- [ ] **Step 9.10: Commit**

```bash
git add requirements.txt flake.nix scripts/ferret_plot/output.py scripts/ferret_plot/formatting.py scripts/ferret_plot/kinds/_shared.py tests/python/conftest.py tests/python/test_output.py
git commit --no-gpg-sign -m "$(cat <<'EOF'
refactor(plot): remove matplotlib dependency

All four plot kinds are on plotly now, so matplotlib leaves the
runtime requirements, the Nix dev shell, and the source tree.
output.emit's matplotlib passthrough goes away; _shared loses
render_heatmap_cell; formatting loses apply_axis; conftest drops
the Agg backend setup and the figure-close fixture.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 10: Add Chrome-gated integration export test

**Files:**
- Create: `tests/python/test_integration_export.py`

- [ ] **Step 10.1: Write the integration test**

Create `tests/python/test_integration_export.py`:

```python
"""End-to-end image export via kaleido.

This test actually launches a headless Chrome through kaleido and is
slow + dependency-heavy. It's marked `integration` and skipped on
hosts without Chrome on PATH. Run with: pytest -m integration.
"""

from __future__ import annotations

import shutil
from pathlib import Path

import pytest
from fixtures import tage_capacity_df

from ferret_plot import cli

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
```

- [ ] **Step 10.2: Register the `integration` marker**

If `pyproject.toml` doesn't yet declare custom pytest markers, add to it:

```toml
[tool.pytest.ini_options]
markers = [
    "integration: hits real external tooling (Chrome/kaleido)",
]
```

- [ ] **Step 10.3: Verify the integration test passes (or skips cleanly)**

Run:

```bash
python3 -m pytest tests/python/test_integration_export.py -v -m integration
```

Expected: either passes (when Chrome is available) or is skipped with the reason "kaleido needs Chrome on PATH".

Also verify the default test run still excludes it cleanly:

```bash
./scripts/test_py.sh
```

Expected: integration test is collected but not deselected by default (it'll skip on Chrome-less hosts). To exclude unconditionally, run `pytest -m "not integration"`.

- [ ] **Step 10.4: Commit**

```bash
git add tests/python/test_integration_export.py pyproject.toml
git commit --no-gpg-sign -m "$(cat <<'EOF'
test(plot): add chrome-gated integration export test

Real end-to-end PNG export through kaleido. Marked `integration`
and skipped when no Chrome binary is on PATH so the default test
run stays Chrome-free; unit tests monkeypatch fig.write_image
instead.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Task 11: Update documentation

**Files:**
- Modify: `README.md:50-58`
- Modify: `docs/cli.md`

- [ ] **Step 11.1: Update the README Quickstart example**

In `README.md`, find the section starting `# Step 3: line plot ...` (around lines 51-58). Replace those lines with:

```sh
# Step 3: line plot. Default --out=*.png writes a static image
# (requires Chrome for kaleido; the Nix dev shell provides it).
python3 scripts/plot.py line /tmp/btb.csv --out=/tmp/btb.png

# Or write interactive HTML (rotate, zoom, hover for exact values):
python3 scripts/plot.py line /tmp/btb.csv --out=/tmp/btb.html

# Or with spacing_bytes on X (branches becomes the legend):
python3 scripts/plot.py line /tmp/btb.csv --x=spacing_bytes --out=/tmp/btb-by-spacing.html

# Or as a 2D heatmap (branches × spacing_bytes, cycles per site as color):
python3 scripts/plot.py heatmap /tmp/btb.csv --out=/tmp/btb-heatmap.html

# Or as an interactive 3D surface (smooth terrain coloring on Z=metric):
python3 scripts/plot.py surface /tmp/btb.csv --x=branch_amount --y=pattern_amount --out=/tmp/btb-surface.html
```

(Use whichever columns actually exist in your `btb.csv`; the existing example used `branches × spacing_bytes`.)

Add a short note after the Quickstart code block:

> Static image formats (`.png` / `.svg` / `.pdf`) require Chrome or Chromium on PATH so kaleido can run a headless export. The Nix dev shell ships `pkgs.chromium`; on other systems install via your package manager or `python -m plotly.io._kaleido install_chrome`. HTML output has no such requirement.

- [ ] **Step 11.2: Update `docs/cli.md`**

Open `docs/cli.md`. Add a new "Output formats" subsection under the global CLI section:

```markdown
## Output formats

Every plot kind shares these output flags:

- `--out=PATH`: write the figure to PATH. Extension determines the
  format unless `--format` overrides it. Supported extensions:
  `.html` (interactive), `.png`, `.svg`, `.pdf`, `.jpg`, `.webp`
  (all static images).
- `--format=html|png|svg|pdf|jpg|webp`: explicit format, overrides
  the extension on `--out`.
- `--html-js=cdn|inline|sibling` (default `cdn`):
  - `cdn` — HTML loads plotly.js from a CDN (~50 KB file, needs network on open).
  - `inline` — HTML includes a full copy of plotly.js (~5 MB file, works offline).
  - `sibling` — first HTML drops a `plotly.min.js` next to it; later HTMLs reuse it (good for output directories).

Omitting `--out` writes a temp HTML and opens it in the system browser.

Static image formats (PNG, SVG, PDF, JPG, WebP) require Chrome or
Chromium on PATH (kaleido uses headless Chrome for rendering). The
Nix dev shell provides `pkgs.chromium`; otherwise install via your
package manager. HTML output has no such requirement.
```

Per-kind, document the new `--cmap` flag wherever the existing flags for `heatmap`, `facets`, and `surface` are listed. (If `docs/cli.md` doesn't have per-kind sections, leave this for a follow-up.)

- [ ] **Step 11.3: Commit**

```bash
git add README.md docs/cli.md
git commit --no-gpg-sign -m "$(cat <<'EOF'
docs(plot): document plotly engine, output formats, --cmap

README Quickstart now shows both .png and .html output and notes
Chrome is required for static image formats. docs/cli.md gains an
"Output formats" subsection covering --format, --html-js, and the
extension-inference rules.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

## Final verification

- [ ] **Step F.1: Run the full Python test suite**

```bash
./scripts/test_py.sh
```

Expected: all unit tests pass; integration test is skipped or passes depending on Chrome availability.

- [ ] **Step F.2: Run ruff**

```bash
./scripts/lint.sh
```

Expected: zero violations.

- [ ] **Step F.3: Confirm no matplotlib references remain**

```bash
grep -rn "matplotlib" scripts/ tests/python/ requirements.txt flake.nix
```

Expected: no matches.

- [ ] **Step F.4: Confirm `git log` shows the expected story**

```bash
git log --oneline 1c1e718..HEAD
```

Expected: roughly 11 commits, one per task, plus the design-doc commit (`1c1e718`).

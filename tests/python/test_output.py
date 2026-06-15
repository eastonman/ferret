"""Tests for ferret_plot.output.emit."""

from __future__ import annotations

import sys
import types
from pathlib import Path
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
        write_calls = []
        monkeypatch.setattr(output.webbrowser, "open", opened.append)
        monkeypatch.setattr(fig, "write_html", lambda *a, **kw: write_calls.append((a, kw)))
        output.emit(fig, out=None, fmt=None, html_js="cdn")
        assert len(write_calls) == 1
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
        with pytest.raises(PlotError, match="Chrome or Chromium"):
            output.emit(fig, out=str(tmp_path / "x.png"), fmt=None, html_js="cdn")


class TestNonCanvasValueError:
    def test_non_canvas_value_error_becomes_plot_error(self, tmp_path, monkeypatch):
        fig = _fig()

        def fail_write_image(*_args, **_kwargs):
            raise ValueError("kaleido internal: unexpected codec failure")

        monkeypatch.setattr(output, "_chrome_available", lambda: True)
        monkeypatch.setattr(fig, "write_image", fail_write_image)

        with pytest.raises(PlotError, match="image export failed"):
            output.emit(fig, out=str(tmp_path / "x.png"), fmt=None, html_js="cdn")

    def test_non_canvas_value_error_preserves_original_text(self, tmp_path, monkeypatch):
        fig = _fig()
        original_msg = "kaleido: some unexpected internal problem"

        def fail_write_image(*_args, **_kwargs):
            raise ValueError(original_msg)

        monkeypatch.setattr(output, "_chrome_available", lambda: True)
        monkeypatch.setattr(fig, "write_image", fail_write_image)

        with pytest.raises(PlotError, match=original_msg):
            output.emit(fig, out=str(tmp_path / "x.png"), fmt=None, html_js="cdn")


class TestSurfaceWebglFallback:
    def test_surface_png_uses_chromium_webgl_when_kaleido_canvas_fails(self, tmp_path, monkeypatch):
        fig = go.Figure(data=[go.Surface(z=[[1.0, 2.0], [3.0, 4.0]])])

        def fail_write_image(*_args, **_kwargs):
            raise ValueError("Transform failed with error code 525: error creating static canvas/context")

        calls = []

        def fake_webgl(fig_arg, out_arg, *, width, height):
            calls.append((fig_arg, out_arg, width, height))
            Path(out_arg).write_bytes(b"\x89PNG\r\n\x1a\n")

        monkeypatch.setattr(output, "_chrome_available", lambda: True)
        monkeypatch.setattr(fig, "write_image", fail_write_image)
        monkeypatch.setattr(output, "_write_chromium_webgl_png", fake_webgl)

        out = tmp_path / "surface.png"
        output.emit(fig, out=str(out), fmt=None, html_js="cdn")

        assert calls == [(fig, str(out), 2400, 1000)]
        assert out.read_bytes() == b"\x89PNG\r\n\x1a\n"


class TestChromeAvailable:
    """Direct tests of _chrome_available's discovery paths.

    Patches every other discovery branch off so each test isolates the
    one path it's checking.
    """

    def _isolate(self, monkeypatch):
        monkeypatch.setattr(output, "_kaleido_bundles_chrome", lambda: False)
        monkeypatch.delenv("BROWSER_PATH", raising=False)
        monkeypatch.setattr(output.shutil, "which", lambda _: None)
        monkeypatch.setattr(output.os.path, "isfile", lambda _: False)

    def test_returns_true_when_kaleido_v0(self, monkeypatch):
        monkeypatch.setattr(output, "_kaleido_bundles_chrome", lambda: True)
        assert output._chrome_available() is True

    def test_returns_true_when_browser_path_set(self, tmp_path, monkeypatch):
        self._isolate(monkeypatch)
        binary = tmp_path / "chrome"
        binary.write_text("#!/bin/sh\nexit 0\n")
        binary.chmod(0o755)
        monkeypatch.setenv("BROWSER_PATH", str(binary))
        monkeypatch.setattr(output.os.path, "isfile", lambda p: p == str(binary))
        monkeypatch.setattr(output.os, "access", lambda p, _: p == str(binary))
        assert output._chrome_available() is True

    def test_returns_true_when_chrome_on_path(self, monkeypatch):
        self._isolate(monkeypatch)
        monkeypatch.setattr(
            output.shutil, "which", lambda name: "/usr/bin/" + name if name == "google-chrome" else None
        )
        assert output._chrome_available() is True

    def test_returns_true_when_macos_chrome_app_exists(self, monkeypatch):
        self._isolate(monkeypatch)
        target = "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
        monkeypatch.setattr(output.os.path, "isfile", lambda p: p == target)
        monkeypatch.setattr(output.os, "access", lambda p, _: p == target)
        assert output._chrome_available() is True

    def test_returns_false_when_nothing_available(self, monkeypatch):
        self._isolate(monkeypatch)
        monkeypatch.setattr(output.os, "access", lambda _p, _m: False)
        assert output._chrome_available() is False


class TestKaleidoVersionDetection:
    def test_v0_detected_as_bundled(self, monkeypatch):
        fake = types.ModuleType("kaleido")
        fake.__version__ = "0.2.1"
        monkeypatch.setitem(sys.modules, "kaleido", fake)
        assert output._kaleido_bundles_chrome() is True

    def test_v1_detected_as_external(self, monkeypatch):
        fake = types.ModuleType("kaleido")
        fake.__version__ = "1.0.0"
        monkeypatch.setitem(sys.modules, "kaleido", fake)
        assert output._kaleido_bundles_chrome() is False

"""argparse subparsers and dispatch for the plot script.

main(argv) is the entry point; argv defaults to sys.argv[1:].
Each subparser attaches its kind's `make_figure` via set_defaults(handler=...),
so adding a fourth kind is a one-place change here plus the new module.
"""

from __future__ import annotations

import argparse
import sys

import pandas as pd

from ferret_plot import output
from ferret_plot.errors import PlotError

# The string value; avoids importing _shared (which pulls in plotly) at startup.
_DEFAULT_CMAP = "turbo"

EXIT_USER_ERROR = 2


def _line_handler(df, args):
    from ferret_plot.kinds.line import make_figure  # noqa: PLC0415  # lazy so --help skips plotly import

    return make_figure(df, args)


def _heatmap_handler(df, args):
    from ferret_plot.kinds.heatmap import make_figure  # noqa: PLC0415  # lazy so --help skips plotly import

    return make_figure(df, args)


def _surface_handler(df, args):
    from ferret_plot.kinds.surface import make_figure  # noqa: PLC0415  # lazy so --help skips plotly import

    return make_figure(df, args)


def _facets_handler(df, args):
    from ferret_plot.kinds.facets import make_figure  # noqa: PLC0415  # lazy so --help skips plotly import

    return make_figure(df, args)


def _add_common(sp: argparse.ArgumentParser) -> None:
    sp.add_argument("csv")
    sp.add_argument("--out", default=None, help="output image path; omitted = open HTML in browser")
    sp.add_argument(
        "--format",
        default=None,
        choices=sorted(output.KNOWN_FORMATS),
        help="output format override (default: infer from --out extension)",
    )
    sp.add_argument(
        "--html-js",
        dest="html_js",
        default="cdn",
        choices=output.HTML_JS_CHOICES,
        help="how to bundle plotly.js in HTML output (default: cdn)",
    )
    sp.add_argument("--benchmark", default=None, help="override registry lookup (rare)")
    sp.add_argument("--metric", default="auto", choices=["auto", "cycles", "ns"])
    sp.add_argument("--stat", default="min", choices=["min", "median"])


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        prog="plot.py",
        description="Plot a ferret CSV as a line plot, heatmap, facet grid, or 3D surface.",
    )
    sub = ap.add_subparsers(dest="kind", required=True)

    line = sub.add_parser("line", help="line plot with series fan-out")
    _add_common(line)
    line.add_argument("--x", default=None, help="X-axis column")
    line.add_argument(
        "--xscale",
        default=None,
        choices=["log", "linear"],
        help="X-axis scale (default: log base 2, or registry hint)",
    )
    line.add_argument("--series", default=None, help="comma-separated columns to use as series grouping")
    line.add_argument("--ymax", type=float, default=None, help="upper limit for the Y axis")
    line.set_defaults(handler=_line_handler)

    heat = sub.add_parser("heatmap", help="2D heatmap over two varying axes")
    _add_common(heat)
    heat.add_argument("--x", default=None, help="X-axis column")
    heat.add_argument("--y", default=None, help="Y-axis column")
    heat.add_argument("--logz", action="store_true", help="log color scale")
    heat.add_argument(
        "--cmap", default=_DEFAULT_CMAP, help=f"colorscale name (default: {_DEFAULT_CMAP}, high-contrast)"
    )
    heat.set_defaults(handler=_heatmap_handler)

    surface = sub.add_parser("surface", help="3D surface over two varying axes")
    _add_common(surface)
    surface.add_argument("--x", default=None, help="X-axis column")
    surface.add_argument("--y", default=None, help="Y-axis column")
    surface.add_argument("--logz", action="store_true", help="log color scale")
    surface.add_argument("--elev", type=float, default=20.0, help="3D camera elevation angle")
    surface.add_argument("--azim", type=float, default=-2.0, help="3D camera azimuth angle")
    surface.add_argument(
        "--cmap", default=_DEFAULT_CMAP, help=f"colorscale name (default: {_DEFAULT_CMAP}, high-contrast perceptual)"
    )
    surface.set_defaults(handler=_surface_handler)

    fac = sub.add_parser("facets", help="grid of heatmaps over >=3 varying axes")
    _add_common(fac)
    fac.add_argument("--facet", default=None, help="column to facet on (one subplot per unique value)")
    fac.add_argument("--x", default=None, help="X-axis column (per subplot)")
    fac.add_argument("--y", default=None, help="Y-axis column (per subplot)")
    fac.add_argument("--logz", action="store_true", help="log color scale")
    fac.add_argument("--cmap", default=_DEFAULT_CMAP, help=f"colorscale name (default: {_DEFAULT_CMAP}, high-contrast)")
    fac.set_defaults(handler=_facets_handler)

    return ap


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

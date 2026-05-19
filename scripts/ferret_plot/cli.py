"""argparse subparsers and dispatch for the plot script.

main(argv) is the entry point; argv defaults to sys.argv[1:].
Each subparser attaches its kind's `make_figure` via set_defaults(handler=...),
so adding a fourth kind is a one-place change here plus the new module.
"""

from __future__ import annotations

import argparse
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.figure import Figure

from ferret_plot.errors import PlotError
from ferret_plot.kinds import facets as facets_kind
from ferret_plot.kinds import heatmap as heatmap_kind
from ferret_plot.kinds import line as line_kind
from ferret_plot.kinds import surface as surface_kind

EXIT_USER_ERROR = 2


def _add_common(sp: argparse.ArgumentParser) -> None:
    sp.add_argument("csv")
    sp.add_argument("--out", default=None, help="output image path; omitted = plt.show()")
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
    line.set_defaults(handler=line_kind.make_figure)

    heat = sub.add_parser("heatmap", help="2D heatmap over two varying axes")
    _add_common(heat)
    heat.add_argument("--x", default=None, help="X-axis column")
    heat.add_argument("--y", default=None, help="Y-axis column")
    heat.add_argument("--logz", action="store_true", help="log color scale")
    heat.set_defaults(handler=heatmap_kind.make_figure)

    surface = sub.add_parser("surface", help="3D surface over two varying axes")
    _add_common(surface)
    surface.add_argument("--x", default=None, help="X-axis column")
    surface.add_argument("--y", default=None, help="Y-axis column")
    surface.add_argument("--logz", action="store_true", help="log color scale")
    surface.add_argument("--elev", type=float, default=20.0, help="3D camera elevation angle")
    surface.add_argument("--azim", type=float, default=-13.0, help="3D camera azimuth angle")
    surface.set_defaults(handler=surface_kind.make_figure)

    fac = sub.add_parser("facets", help="grid of heatmaps over >=3 varying axes")
    _add_common(fac)
    fac.add_argument("--facet", default=None, help="column to facet on (one subplot per unique value)")
    fac.add_argument("--x", default=None, help="X-axis column (per subplot)")
    fac.add_argument("--y", default=None, help="Y-axis column (per subplot)")
    fac.add_argument("--logz", action="store_true", help="log color scale")
    fac.set_defaults(handler=facets_kind.make_figure)

    return ap


def _emit(fig: Figure, out: str | None) -> None:
    if out:
        fig.savefig(out, bbox_inches="tight", dpi=400)
        plt.close(fig)
    else:
        plt.show()


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        df = pd.read_csv(args.csv)
        fig = args.handler(df, args)
        _emit(fig, args.out)
        return 0
    except (PlotError, FileNotFoundError) as e:
        print(f"plot.py: {e}", file=sys.stderr)
        return EXIT_USER_ERROR

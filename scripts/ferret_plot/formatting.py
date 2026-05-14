"""Tick formatting helpers shared across plot kinds."""

from __future__ import annotations

from collections.abc import Iterable

import matplotlib.ticker as mticker
from matplotlib.axis import Axis


def human_readable(x, _pos: int | None = None) -> str:
    """Format a numeric tick with G/M/K suffix when >= 2^10.

    Returns str(x) verbatim for non-numeric values (e.g. string-valued
    axis columns used as a facet or categorical axis). Non-positive
    numerics fall through to default %g formatting.
    """
    if not isinstance(x, (int, float)) or isinstance(x, bool):
        return str(x)
    if x <= 0:
        return f"{x:g}"
    for unit, scale in (("G", 1 << 30), ("M", 1 << 20), ("K", 1 << 10)):
        if x >= scale:
            return f"{x / scale:g}{unit}"
    return f"{x:g}"


def apply_axis(axis: Axis, values: Iterable[float], *, scale: str = "log") -> None:
    """Configure a matplotlib axis for a sweep.

    scale='log' applies log base 2 with explicit ticks at the unique sweep
    values; matplotlib's auto-log-base-2 ticking is poor for the small
    power-of-2 ranges ferret typically produces, so we set them manually.

    scale='linear' uses matplotlib's default linear scale with auto-ticking,
    suitable for non-power-of-2 sweeps (e.g. nested_call_depth's
    depth=1..64). The human_readable formatter is preserved so K/M/G
    suffixing stays consistent across plot kinds.
    """
    # axis_name is a class attribute on matplotlib's XAxis/YAxis ('x' / 'y');
    # stable in practice across matplotlib releases.
    if scale == "log":
        if axis.axis_name == "x":
            axis.axes.set_xscale("log", base=2)
        else:
            axis.axes.set_yscale("log", base=2)
        # Callers pass df[col].unique(), which is already deduplicated; sort
        # only — values come from pandas in first-occurrence order.
        axis.set_ticks(sorted(values))
        axis.set_minor_formatter(mticker.NullFormatter())
    axis.set_major_formatter(mticker.FuncFormatter(human_readable))

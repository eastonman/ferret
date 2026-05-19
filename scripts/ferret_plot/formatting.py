"""Tick formatting helpers shared across plot kinds."""

from __future__ import annotations

import math
import numbers
from collections.abc import Sequence

# Soft caps on explicit ticks at the figure widths we use (figsize 8" / 4"
# per cell). Generic data hits the lower cap once K/M/G suffixing widens
# labels; power-of-2 data tolerates more because its labels stay short and
# align on a clean grid the eye already expects.
MAX_EXPLICIT_TICKS = 10
MAX_POW2_TICKS = 16
# Anchor on existing powers of 2 once at least this many appear in the
# sweep. Three is enough to read as a power-of-2 grid (octave, octave) —
# below that, striding gives a more uniform look.
MIN_POW2_ANCHORS = 3
_POW2_EPS = 1e-9


def _log2_int(v: object) -> int | None:
    """Return n if v == 2**n for non-negative integer n, else None.

    Accepts any ``numbers.Real`` so numpy scalars (e.g. ``np.int64``)
    coming straight from a DataFrame column are recognized too.
    """
    if isinstance(v, bool) or not isinstance(v, numbers.Real) or v <= 0:
        return None
    lv = math.log2(float(v))
    rounded = round(lv)
    if abs(lv - rounded) > _POW2_EPS or rounded < 0:
        return None
    return int(rounded)


def decimate_indices(labels: Sequence[object]) -> list[int]:
    """Pick which positions in ``labels`` to keep as ticks.

    When the value set contains enough powers of 2 (covers both pure
    pow-of-2 sweeps and dense log sweeps that interpolate between
    octaves like 32, 35, 38, …, 64, …), tick only on those powers —
    further strided in log2 space if there are too many. Everything
    else falls back to fixed-stride decimation. The last index is
    always kept so the visible extent matches the data.
    """
    n = len(labels)
    if n <= MAX_EXPLICIT_TICKS:
        return list(range(n))

    pow2 = [(i, lv) for i, v in enumerate(labels) if (lv := _log2_int(v)) is not None]
    if len(pow2) >= MIN_POW2_ANCHORS:
        if len(pow2) > MAX_POW2_TICKS:
            anchor, last = pow2[0][1], pow2[-1][1]
            stride = max(1, math.ceil((last - anchor) / (MAX_POW2_TICKS - 1)))
            pow2 = [(i, lv) for i, lv in pow2 if (lv - anchor) % stride == 0]
        return [i for i, _ in pow2]

    stride = math.ceil(n / MAX_EXPLICIT_TICKS)
    kept = list(range(0, n, stride))
    if kept[-1] != n - 1:
        kept.append(n - 1)
    return kept


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

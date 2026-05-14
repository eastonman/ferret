"""Plotter-specific exception types."""


class PlotError(Exception):
    """User-facing error: the CLI converts this to a clean exit code 2.

    Raise for missing-but-required columns, mixed-benchmark CSVs,
    explicitly-requested metrics that don't exist, etc. Anything that
    is *not* a real bug.
    """

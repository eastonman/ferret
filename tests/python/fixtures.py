"""Synthetic DataFrames mirroring ferret CSV output.

One builder per registered benchmark. Column names and order match
what CsvWriter (src/output/csv.cpp) emits.
"""

from __future__ import annotations

import pandas as pd


def _add_freq(row: dict, ns: float, freq_hz: float) -> None:
    """Stamp the cycles + freq_hz columns onto a row when with_freq=True."""
    cycles = ns * (freq_hz / 1e9)
    row["cycles_per_site_min"] = cycles
    row["cycles_per_site_median"] = cycles
    row["freq_hz"] = freq_hz


def dbf_df(
    *,
    branches: tuple[int, ...] = (1, 2, 4, 8),
    spacing: tuple[int, ...] = (16, 32, 64),
    with_freq: bool = True,
) -> pd.DataFrame:
    """direct_branch_footprint synthetic frame."""
    rows = []
    for b in branches:
        for s in spacing:
            ns = 0.5 + b * 0.01 + s * 0.001
            row = {
                "benchmark": "direct_branch_footprint",
                "branches": b,
                "spacing_bytes": s,
                "ticks_min": 1000 + b * s,
                "ticks_median": 1000 + b * s,
                "iters": 1,
                "sites_per_iter": b,
                "reps": 7,
                "ns_per_site_min": ns,
                "ns_per_site_median": ns,
            }
            if with_freq:
                _add_freq(row, ns, freq_hz=4.0e9)
            rows.append(row)
    return pd.DataFrame(rows)


def dct_df(
    *,
    chain_lengths: tuple[int, ...] = (100_000_000,),
    with_freq: bool = True,
) -> pd.DataFrame:
    """dependent_chain_throughput synthetic frame."""
    rows = []
    for cl in chain_lengths:
        row = {
            "benchmark": "dependent_chain_throughput",
            "chain_length": cl,
            "ticks_min": cl,
            "ticks_median": cl,
            "iters": 1,
            "sites_per_iter": cl,
            "reps": 7,
            "ns_per_site_min": 0.221,
            "ns_per_site_median": 0.221,
        }
        if with_freq:
            _add_freq(row, ns=0.221, freq_hz=4.521e9)
        rows.append(row)
    return pd.DataFrame(rows)


def three_axis_df(
    *,
    branches: tuple[int, ...] = (1, 2, 4),
    spacing: tuple[int, ...] = (16, 32),
    variants: tuple[str, ...] = ("a", "b"),
    with_freq: bool = True,
) -> pd.DataFrame:
    """Synthetic 3-axis frame: branches × spacing × variant.

    'variant' stands in for a future option-as-axis case (e.g. a TAGE
    predictor configuration). Not a real ferret column today.
    """
    rows = []
    for b in branches:
        for s in spacing:
            for v in variants:
                ns = 0.5 + b * 0.01 + s * 0.001 + len(v) * 0.1
                row = {
                    "benchmark": "synthetic_three_axis",
                    "branches": b,
                    "spacing_bytes": s,
                    "variant": v,
                    "ticks_min": 1000,
                    "ticks_median": 1000,
                    "iters": 1,
                    "sites_per_iter": b,
                    "reps": 7,
                    "ns_per_site_min": ns,
                    "ns_per_site_median": ns,
                }
                if with_freq:
                    _add_freq(row, ns, freq_hz=4.0e9)
                rows.append(row)
    return pd.DataFrame(rows)


def tage_capacity_df(
    *,
    branch_amounts: tuple[int, ...] = (64, 128, 256, 512),
    pattern_amounts: tuple[int, ...] = (4, 8, 16),
    with_freq: bool = True,
) -> pd.DataFrame:
    """Synthetic two-axis frame for a TAGE-capacity-style plot."""
    rows = []
    for branch_amount in branch_amounts:
        for pattern_amount in pattern_amounts:
            ns = 0.4 + branch_amount * 0.002 + pattern_amount * 0.03
            row = {
                "benchmark": "synthetic_tage_capacity",
                "branch_amount": branch_amount,
                "pattern_amount": pattern_amount,
                "ticks_min": 1000 + branch_amount * pattern_amount,
                "ticks_median": 1000 + branch_amount * pattern_amount,
                "iters": 1,
                "sites_per_iter": branch_amount,
                "reps": 7,
                "ns_per_site_min": ns,
                "ns_per_site_median": ns,
            }
            if with_freq:
                _add_freq(row, ns, freq_hz=4.0e9)
            rows.append(row)
    return pd.DataFrame(rows)


def nced_df(
    *,
    depths: tuple[int, ...] = (1, 2, 4, 8, 16),
    variants: tuple[int, ...] = (1,),
    with_freq: bool = True,
) -> pd.DataFrame:
    """nested_call_depth synthetic frame.

    Axes match the C++ benchmark: `depth` (plain range, 1..64 in practice;
    a small subset here) and `variant` (discrete kernel id 0/1/2).
    """
    rows = []
    for d in depths:
        for v in variants:
            ns = 0.5 + d * 0.05 + v * 0.01
            row = {
                "benchmark": "nested_call_depth",
                "depth": d,
                "variant": v,
                "ticks_min": 1000 + d * 10,
                "ticks_median": 1000 + d * 10,
                "iters": 1,
                "sites_per_iter": d,
                "reps": 7,
                "ns_per_site_min": ns,
                "ns_per_site_median": ns,
            }
            if with_freq:
                _add_freq(row, ns, freq_hz=4.0e9)
            rows.append(row)
    return pd.DataFrame(rows)

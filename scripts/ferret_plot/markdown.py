"""Markdown summaries for benchmark CSV artifacts."""

from __future__ import annotations

import math
import numbers
from pathlib import Path

import pandas as pd


def _fmt(value: object) -> str:
    if value is None or pd.isna(value):
        return ""
    if isinstance(value, str):
        return value
    if isinstance(value, numbers.Integral) and not isinstance(value, bool):
        return str(value)
    try:
        f = float(value)
    except (TypeError, ValueError):
        return str(value)
    if not math.isfinite(f):
        return str(value)
    return f"{f:.4g}"


def dependent_chain_markdown(df: pd.DataFrame) -> str:
    """Render dependent_chain_throughput rows as a compact Markdown table."""
    headers = [
        "chain_length",
        "ns_per_site_min",
        "ns_per_site_median",
        "estimated_ghz",
    ]
    if "cycles_per_site_min" in df.columns:
        headers.extend(["cycles_per_site_min", "cycles_per_site_median"])

    lines = [
        "# dependent_chain_throughput",
        "",
        "| " + " | ".join(headers) + " |",
        "| " + " | ".join("---" for _ in headers) + " |",
    ]
    for _, row in df.iterrows():
        ns_min = row.get("ns_per_site_min")
        try:
            estimated_ghz = 1.0 / float(ns_min)
        except (TypeError, ValueError, ZeroDivisionError):
            estimated_ghz = math.nan
        values = {
            "chain_length": _fmt(row.get("chain_length")),
            "ns_per_site_min": _fmt(ns_min),
            "ns_per_site_median": _fmt(row.get("ns_per_site_median")),
            "estimated_ghz": _fmt(estimated_ghz),
            "cycles_per_site_min": _fmt(row.get("cycles_per_site_min")),
            "cycles_per_site_median": _fmt(row.get("cycles_per_site_median")),
        }
        lines.append("| " + " | ".join(values[h] for h in headers) + " |")
    return "\n".join(lines) + "\n"


def write_dependent_chain_markdown(csv_path: str | Path, out_path: str | Path) -> None:
    df = pd.read_csv(csv_path)
    Path(out_path).write_text(dependent_chain_markdown(df), encoding="utf-8")

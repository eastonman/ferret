#!/usr/bin/env python3
"""Validate that a dependent-chain benchmark's chain is actually serial.

Usage: python scripts/chain_slope.py run-alu1.csv run-alu2.csv ...

Every store->load benchmark reports a per-link cost of the form

    cycles_per_site = F + alu_ops

where F is the store-to-load forward cost. That identity has a testable
consequence: plotting cycles_per_site against alu_ops must give a slope
of exactly 1.0, because each carry op is one dependent ADD. This script
fits that line and fails if the slope is off.

Why it matters. A slope below 1.0 means the chain is not fully serial,
and the usual cause is nasty: Apple cores carry a per-PC last-value load
predictor, so if a static load ever returns the same value twice the
predictor severs the chain and the loop runs at issue throughput. The
benchmark then reports roughly 0.4 cycles per link where the truth is
3.0 -- an error that looks like a discovery. A slope above 1.0 means the
carry ops are costing more than a cycle each, so subtracting alu_ops to
recover F under-corrects.

Feed it CSVs from otherwise-identical runs that differ only in
--alu_ops. Rows are grouped by every axis column they share, and each
group is fitted separately.
"""

from __future__ import annotations

import csv
import sys

# Slope must be 1.0. Real measurements wobble, so allow a small band.
TOLERANCE = 0.05

# Below this the chain is not a chain any more: cycles/link has stopped
# tracking alu_ops, which is what a severed dependency looks like.
SEVERED_SLOPE = 0.5

METADATA = frozenset(
    {
        "benchmark",
        "seed",
        "ticks_min",
        "ticks_median",
        "iters",
        "sites_per_iter",
        "reps",
        "ns_per_site_min",
        "ns_per_site_median",
        "cycles_per_site_min",
        "cycles_per_site_median",
        "freq_hz",
    }
)


def _fit(points):
    """Least-squares slope/intercept of y against x."""
    n = len(points)
    mx = sum(x for x, _ in points) / n
    my = sum(y for _, y in points) / n
    den = sum((x - mx) ** 2 for x, _ in points)
    if den == 0.0:
        return None
    slope = sum((x - mx) * (y - my) for x, y in points) / den
    return slope, my - slope * mx


def load(paths):
    """Group rows by their axis columns; return {key: [(alu_ops, cycles)]}."""
    groups: dict[tuple, list] = {}
    for path in paths:
        with open(path, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                if not row.get("cycles_per_site_min"):
                    continue  # JIT failure, or no --freq was supplied
                if "alu_ops" not in row:
                    raise SystemExit(f"{path}: no alu_ops column; this benchmark has no chain to check")
                axes = tuple(sorted((k, v) for k, v in row.items() if k not in METADATA and k != "alu_ops"))
                groups.setdefault(axes, []).append((float(row["alu_ops"]), float(row["cycles_per_site_min"])))
    return groups


def main(argv):
    if len(argv) < 2:  # noqa: PLR2004
        print("usage: chain_slope.py FILE.csv [FILE.csv ...]", file=sys.stderr)
        return 2

    groups = load(argv[1:])
    if not groups:
        print("chain_slope.py: no usable rows (need --freq so cycles_per_site_min is populated)", file=sys.stderr)
        return 2

    failed = 0
    for axes, points in sorted(groups.items()):
        label = " ".join(f"{k}={v}" for k, v in axes) or "(no axes)"
        if len({x for x, _ in points}) < 2:  # noqa: PLR2004
            print(f"SKIP  {label}: only one alu_ops value, nothing to fit")
            continue
        fitted = _fit(points)
        if fitted is None:
            print(f"SKIP  {label}: degenerate fit")
            continue
        slope, intercept = fitted

        if slope < SEVERED_SLOPE:
            verdict, note, fatal = "FAIL", "chain severed -- cycles/link barely tracks alu_ops", True
        elif slope > 1.0 + TOLERANCE:
            verdict, note, fatal = "FAIL", "carry ops cost more than a cycle each", True
        elif slope < 1.0 - TOLERANCE:
            verdict, note, fatal = "WARN", "sublinear: part of the load latency overlaps the chain", False
        else:
            verdict, note, fatal = "ok  ", "", False

        failed += int(fatal)
        detail = f" -- {note}" if note else ""
        print(f"{verdict}  {label}: slope={slope:.3f} forward_cost={intercept:.2f} ({len(points)} points){detail}")
        if note:
            # F is not a single number here, so show how it moves.
            per = "  ".join(f"alu={int(x)}:{y - x:.2f}" for x, y in sorted(points))
            print(f"        forward cost per point: {per}")

    if failed:
        print(
            f"\nchain_slope.py: {failed} group(s) failed.\n"
            "  slope near zero  -> the dependent chain was severed, most often by load\n"
            "                      value prediction when a static load returns a constant.\n"
            "                      Those rows report issue throughput, not latency.\n"
            "  slope above 1.0  -> each carry op costs more than a cycle, so recovering\n"
            "                      the forward cost by subtracting alu_ops under-corrects.\n"
            "A WARN row is not a benchmark bug: a slope moderately below 1.0 means the\n"
            "machine overlaps some of the load's setup with the chain, so the forward\n"
            "cost is a range rather than a constant. Quote it per alu_ops, not once.",
            file=sys.stderr,
        )
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))

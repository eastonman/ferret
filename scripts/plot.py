#!/usr/bin/env python3
"""Plot a ferret CSV as a cliff curve.

Usage:
  python scripts/plot.py FILE.csv [--out=plot.png] [--x=COLUMN]

Picks Y as cycles_per_site_min if present, else ns_per_site_min. The
X column defaults to the first non-`benchmark` column whose values
vary across rows; pass --x=NAME to override. Any remaining axis-like
columns become series (one curve per unique value).
"""

import argparse
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd


METADATA_COLS = {
    "benchmark", "ticks_min", "ticks_median", "iters",
    "sites_per_iter", "reps", "ns_per_site_min",
    "ns_per_site_median", "cycles_per_site_min",
    "cycles_per_site_median", "freq_hz",
}


def auto_x_column(df, metric_col):
    for col in df.columns:
        if col in METADATA_COLS or col == metric_col:
            continue
        if df[col].nunique() > 1:
            return col
    raise SystemExit("plot.py: no varying axis column to use as X")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--out", default=None,
                    help="output image path; default shows interactively")
    ap.add_argument("--x", default=None, help="X-axis column name")
    args = ap.parse_args()

    df = pd.read_csv(args.csv)

    if "cycles_per_site_min" in df.columns and df["cycles_per_site_min"].notna().any():
        ycol = "cycles_per_site_min"
        ylabel = "cycles per site"
    else:
        ycol = "ns_per_site_min"
        ylabel = "ns per site"

    xcol = args.x or auto_x_column(df, ycol)

    series_cols = [c for c in df.columns
                   if c not in METADATA_COLS and c not in {xcol, ycol}]

    fig, ax = plt.subplots(figsize=(8, 5))
    if series_cols:
        for keys, sub in df.groupby(series_cols):
            label_keys = keys if isinstance(keys, tuple) else (keys,)
            label = ", ".join(f"{c}={v}" for c, v in zip(series_cols, label_keys))
            ax.plot(sub[xcol], sub[ycol], marker="o", label=label)
        ax.legend()
    else:
        ax.plot(df[xcol], df[ycol], marker="o")

    ax.set_xscale("log", base=2)
    ax.set_xlabel(xcol)
    ax.set_ylabel(ylabel)
    bench = df["benchmark"].iloc[0] if "benchmark" in df.columns else "ferret"
    ax.set_title(f"{bench}: {ylabel} vs {xcol}")
    ax.grid(True, which="both", linestyle="--", alpha=0.4)

    if args.out:
        fig.savefig(args.out, bbox_inches="tight")
    else:
        plt.show()


if __name__ == "__main__":
    main()

# `scripts/plot.py` Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the single-file `scripts/plot.py` with a small `ferret_plot` package exposing `line`/`heatmap`/`facets` subcommands, a per-benchmark defaults registry, and pytest coverage. First-class `spacing_bytes`-as-X invocation for `direct_branch_footprint`.

**Architecture:** Five commits, each independently buildable and test-green. The package lives at `scripts/ferret_plot/`; `scripts/plot.py` keeps its README-facing path as a `sys.path`-injecting entry shim. Tests live under `tests/python/` and run via a new `scripts/test_py.sh` wrapper. No C++ touched.

**Tech Stack:** Python 3.11+, pandas, matplotlib (Agg backend), pytest, ruff.

**Spec:** `docs/superpowers/specs/2026-05-14-plot-redesign-design.md`

**Baseline:** `b0f01f6` on `main`. Working branch: `feat/plot-redesign`.

---

## Pre-flight

- [ ] **Verify clean C++ baseline**

Run from the worktree root (`/Users/easton/WorkingSpace/project/ferret/feat/plot-redesign`):

```bash
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: every existing test passes. If any fail, stop — every later step assumes a clean baseline. This plan touches no C++ code, so C++ tests should remain green at every commit.

- [ ] **Verify Python tooling is available**

```bash
python3 -c "import pandas, matplotlib; print(pandas.__version__, matplotlib.__version__)"
python3 -m pytest --version
ruff --version
```

Expected: all three commands print versions and exit 0. If `pytest` is missing, install it with `python3 -m pip install pytest` (or `pip install pytest` inside the project's existing Python env — the repo has no `requirements.txt`, so use whatever environment the user runs `scripts/plot.py` from today).

- [ ] **Capture the line-plot equivalence baseline**

```bash
build/ferret run direct_branch_footprint --branches=1,2,4,8 --spacing_bytes=64 \
  --reps=3 --warmup=1 --seed=1 --freq=4.0GHz --out=/tmp/ferret-plot-baseline.csv
python3 scripts/plot.py /tmp/ferret-plot-baseline.csv --out=/tmp/ferret-plot-baseline.png
file /tmp/ferret-plot-baseline.png
```

Expected last line: `PNG image data, ...`. Keep `/tmp/ferret-plot-baseline.csv` and the PNG — Task 3 uses the same CSV to verify the new `plot.py line` subcommand produces a similarly-shaped figure, and Task 1's `plot.py` migration must reproduce the same PNG byte-stable.

---

## Task 1: `refactor(plot): extract metadata + metric resolution into ferret_plot.columns`

**Goal of commit:** Introduce the `ferret_plot` package skeleton with `errors`, `columns`, `formatting`. Migrate `scripts/plot.py` to consume them. CLI shape unchanged. Add pytest scaffolding.

**Files:**
- Create: `scripts/ferret_plot/__init__.py`
- Create: `scripts/ferret_plot/errors.py`
- Create: `scripts/ferret_plot/columns.py`
- Create: `scripts/ferret_plot/formatting.py`
- Create: `scripts/test_py.sh`
- Create: `tests/python/conftest.py`
- Create: `tests/python/fixtures.py`
- Create: `tests/python/test_columns.py`
- Modify: `scripts/plot.py` (use new helpers internally; CLI unchanged)
- Modify: `scripts/lint.sh` (extend ruff to `tests/python/`)
- Modify: `scripts/format.sh` (extend ruff to `tests/python/`)

### Step 1.1: Create the pytest scaffolding

- [ ] Create `tests/python/conftest.py`:

```python
"""Shared pytest config for ferret_plot tests.

Adds scripts/ to sys.path so `import ferret_plot` works without an
editable install, and forces matplotlib's Agg backend before any test
imports it.
"""

import sys
from pathlib import Path

import matplotlib

matplotlib.use("Agg")

_SCRIPTS = Path(__file__).resolve().parent.parent.parent / "scripts"
sys.path.insert(0, str(_SCRIPTS))
```

- [ ] Create `tests/python/fixtures.py`:

```python
"""Synthetic DataFrames mirroring ferret CSV output.

One builder per registered benchmark. Column names, dtypes, and order
match what CsvWriter (src/output/csv.cpp) emits.
"""

from __future__ import annotations

import pandas as pd


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
                row["cycles_per_site_min"] = ns * 4.0
                row["cycles_per_site_median"] = ns * 4.0
                row["freq_hz"] = 4.0e9
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
            row["cycles_per_site_min"] = 1.0
            row["cycles_per_site_median"] = 1.0
            row["freq_hz"] = 4.521e9
        rows.append(row)
    return pd.DataFrame(rows)
```

- [ ] Create `scripts/test_py.sh`:

```bash
#!/usr/bin/env bash
# Run the ferret_plot pytest suite from the repo root.
# Returns non-zero on any test failure.

set -euo pipefail

cd "$(dirname "$0")/.."

exec python3 -m pytest tests/python "$@"
```

- [ ] Make it executable:

```bash
chmod +x scripts/test_py.sh
```

### Step 1.2: Write the failing test for `PlotError` and `columns`

- [ ] Create `tests/python/test_columns.py`:

```python
"""Tests for ferret_plot.columns and ferret_plot.errors."""

from __future__ import annotations

import numpy as np
import pandas as pd
import pytest

from ferret_plot import columns as cols
from ferret_plot.errors import PlotError
from fixtures import dbf_df, dct_df


class TestMetadataCols:
    def test_includes_known_metadata(self):
        for name in (
            "benchmark", "ticks_min", "ticks_median", "iters",
            "sites_per_iter", "reps",
            "ns_per_site_min", "ns_per_site_median",
            "cycles_per_site_min", "cycles_per_site_median", "freq_hz",
        ):
            assert name in cols.METADATA_COLS

    def test_is_frozenset(self):
        # Must be immutable so callers can't mutate the project-wide set.
        assert isinstance(cols.METADATA_COLS, frozenset)


class TestResolveMetric:
    def test_auto_prefers_cycles_when_present(self):
        df = dbf_df(with_freq=True)
        m = cols.resolve_metric(df, metric="auto", stat="min")
        assert m.column == "cycles_per_site_min"
        assert m.short == "cycles"
        assert "min" in m.label

    def test_auto_falls_back_to_ns_when_cycles_absent(self):
        df = dbf_df(with_freq=False)
        m = cols.resolve_metric(df, metric="auto", stat="min")
        assert m.column == "ns_per_site_min"
        assert m.short == "ns"

    def test_auto_falls_back_to_ns_when_cycles_all_nan(self):
        df = dbf_df(with_freq=True)
        df["cycles_per_site_min"] = np.nan
        m = cols.resolve_metric(df, metric="auto", stat="min")
        assert m.column == "ns_per_site_min"

    def test_explicit_cycles_raises_when_missing(self):
        df = dbf_df(with_freq=False)
        with pytest.raises(PlotError):
            cols.resolve_metric(df, metric="cycles", stat="min")

    def test_explicit_ns_returns_ns_min(self):
        df = dbf_df(with_freq=True)
        m = cols.resolve_metric(df, metric="ns", stat="min")
        assert m.column == "ns_per_site_min"

    def test_stat_median_resolves_to_median_column(self):
        df = dbf_df(with_freq=True)
        m = cols.resolve_metric(df, metric="cycles", stat="median")
        assert m.column == "cycles_per_site_median"
        assert "median" in m.label


class TestAxisColumns:
    def test_dbf_axes(self):
        df = dbf_df()
        assert cols.axis_columns(df) == ["branches", "spacing_bytes"]

    def test_dct_axes(self):
        df = dct_df()
        assert cols.axis_columns(df) == ["chain_length"]

    def test_varying_axis_columns_filters_constants(self):
        df = dbf_df(branches=(4,), spacing=(16, 32, 64))
        assert cols.varying_axis_columns(df) == ["spacing_bytes"]

    def test_varying_axis_columns_keeps_order(self):
        df = dbf_df()
        assert cols.varying_axis_columns(df) == ["branches", "spacing_bytes"]


class TestPlotError:
    def test_is_an_exception(self):
        assert issubclass(PlotError, Exception)

    def test_carries_message(self):
        e = PlotError("bad thing")
        assert str(e) == "bad thing"
```

### Step 1.3: Verify the tests fail

Run:

```bash
scripts/test_py.sh -q 2>&1 | tail -20
```

Expected: import error along the lines of `ModuleNotFoundError: No module named 'ferret_plot'`. This confirms pytest finds the tests and the package is genuinely missing.

### Step 1.4: Create `ferret_plot/__init__.py`

- [ ] Create `scripts/ferret_plot/__init__.py`:

```python
"""ferret_plot: plot ferret CSV output as line plots, heatmaps, or facet grids.

The package is consumed by the thin `scripts/plot.py` entry shim. It is
not pip-installable; tests prepend `scripts/` to sys.path via
`tests/python/conftest.py`.
"""
```

### Step 1.5: Create `ferret_plot/errors.py`

- [ ] Create `scripts/ferret_plot/errors.py`:

```python
"""Plotter-specific exception types."""


class PlotError(Exception):
    """User-facing error: the CLI converts this to a clean exit code 2.

    Raise for missing-but-required columns, mixed-benchmark CSVs,
    explicitly-requested metrics that don't exist, etc. Anything that
    is *not* a real bug.
    """
```

### Step 1.6: Create `ferret_plot/columns.py`

- [ ] Create `scripts/ferret_plot/columns.py`:

```python
"""Column classification and metric resolution.

A ferret CSV has three classes of columns:
- Metadata: emitted by every benchmark (benchmark name, timing,
  iters, sites_per_iter, reps, ns/cycles columns, freq_hz).
- Axes: per-benchmark sweep parameters (e.g. branches, spacing_bytes).
- (Future) Options: per-benchmark scalar options. Today these appear
  alongside axes in the CSV header; this module treats anything
  non-metadata as an axis column.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Literal

import pandas as pd

from ferret_plot.errors import PlotError

METADATA_COLS: frozenset[str] = frozenset({
    "benchmark",
    "ticks_min", "ticks_median",
    "iters", "sites_per_iter", "reps",
    "ns_per_site_min", "ns_per_site_median",
    "cycles_per_site_min", "cycles_per_site_median",
    "freq_hz",
})


@dataclass(frozen=True)
class Metric:
    column: str
    label: str
    short: Literal["cycles", "ns"]


def resolve_metric(df: pd.DataFrame, *, metric: str, stat: str) -> Metric:
    """Resolve the (metric, stat) request to a concrete CSV column.

    metric ∈ {'auto', 'cycles', 'ns'}; stat ∈ {'min', 'median'}.
    'auto' prefers cycles when the cycles column for that stat has
    at least one non-NaN row, else falls back to ns.

    Raises PlotError if the resolved column is missing or all-NaN
    (typically an explicit --metric=cycles on a freq-less CSV).
    """
    if stat not in ("min", "median"):
        raise PlotError(f"stat must be 'min' or 'median' (got {stat!r})")

    cycles_col = f"cycles_per_site_{stat}"
    ns_col = f"ns_per_site_{stat}"

    def _usable(col: str) -> bool:
        return col in df.columns and df[col].notna().any()

    if metric == "auto":
        chosen = cycles_col if _usable(cycles_col) else ns_col
    elif metric == "cycles":
        chosen = cycles_col
    elif metric == "ns":
        chosen = ns_col
    else:
        raise PlotError(f"metric must be 'auto', 'cycles', or 'ns' (got {metric!r})")

    if not _usable(chosen):
        raise PlotError(
            f"metric column {chosen!r} is missing or all-NaN; "
            f"available columns: {sorted(df.columns.tolist())}"
        )

    short: Literal["cycles", "ns"] = "cycles" if chosen.startswith("cycles_") else "ns"
    label = f"{short} per site ({stat})"
    return Metric(column=chosen, label=label, short=short)


def axis_columns(df: pd.DataFrame) -> list[str]:
    """All non-metadata columns in CSV order."""
    return [c for c in df.columns if c not in METADATA_COLS]


def varying_axis_columns(df: pd.DataFrame) -> list[str]:
    """Axis columns whose values vary across rows.

    A constant-valued axis (e.g. --spacing_bytes=64 sweep with a fixed
    spacing) is an axis in the CSV schema but not interesting to plot.
    """
    return [c for c in axis_columns(df) if df[c].nunique(dropna=False) > 1]
```

### Step 1.7: Create `ferret_plot/formatting.py`

- [ ] Create `scripts/ferret_plot/formatting.py`:

```python
"""Tick formatting helpers shared across plot kinds."""

from __future__ import annotations

from collections.abc import Iterable

import matplotlib.ticker as mticker
from matplotlib.axis import Axis


def human_readable(x: float, _pos: int | None = None) -> str:
    """Format a positive number with G/M/K suffix when >= 2^10.

    Matches the formatter that lived inline in the original plot.py.
    Non-positive values fall through to default %g formatting.
    """
    if x <= 0:
        return f"{x:g}"
    for unit, scale in (("G", 1 << 30), ("M", 1 << 20), ("K", 1 << 10)):
        if x >= scale:
            return f"{x / scale:g}{unit}"
    return f"{x:g}"


def apply_log2_axis(axis: Axis, values: Iterable[float]) -> None:
    """Configure a matplotlib axis for a log2-spaced axis sweep.

    Sets log scale base 2, ticks at the unique sorted values, the
    human_readable major formatter, and suppresses minor tick labels.
    """
    axis.axes.set_xscale("log", base=2) if axis.axis_name == "x" else axis.axes.set_yscale("log", base=2)
    sorted_vals = sorted(set(values))
    axis.set_ticks(sorted_vals)
    axis.set_major_formatter(mticker.FuncFormatter(human_readable))
    axis.set_minor_formatter(mticker.NullFormatter())
```

### Step 1.8: Run the tests, verify they pass

Run:

```bash
scripts/test_py.sh -v 2>&1 | tail -40
```

Expected: every test in `test_columns.py` passes (14 test methods across 4 classes). If any fail, fix the implementation file referenced by the failing assertion before continuing.

### Step 1.9: Migrate `scripts/plot.py` to use the new helpers

- [ ] Replace the contents of `scripts/plot.py` (currently 107 lines):

```python
#!/usr/bin/env python3
"""Plot a ferret CSV as a cliff curve.

Usage:
  python scripts/plot.py FILE.csv [--out=plot.png] [--x=COLUMN]

Picks Y as cycles_per_site_min if present, else ns_per_site_min. The
X column defaults to the first non-`benchmark` column whose values
vary across rows; pass --x=NAME to override. Any remaining axis-like
columns become series (one curve per unique value).
"""

from __future__ import annotations

import argparse
import os
import sys

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pandas as pd

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ferret_plot.columns import resolve_metric, varying_axis_columns
from ferret_plot.errors import PlotError
from ferret_plot.formatting import apply_log2_axis


def _auto_x_column(df: pd.DataFrame, metric_col: str) -> str:
    for col in varying_axis_columns(df):
        if col != metric_col:
            return col
    raise PlotError("no varying axis column to use as X")


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("csv")
    ap.add_argument("--out", default=None, help="output image path; default shows interactively")
    ap.add_argument("--x", default=None, help="X-axis column name")
    ap.add_argument("--ymax", type=float, default=None, help="upper limit for the Y axis")
    args = ap.parse_args()

    try:
        df = pd.read_csv(args.csv)
        metric = resolve_metric(df, metric="auto", stat="min")
        xcol = args.x or _auto_x_column(df, metric.column)
    except PlotError as e:
        print(f"plot.py: {e}", file=sys.stderr)
        return 2

    series_cols = [c for c in varying_axis_columns(df) if c != xcol]

    fig, ax = plt.subplots(figsize=(8, 5))
    if series_cols:
        for keys, sub in df.groupby(series_cols):
            label_keys = keys if isinstance(keys, tuple) else (keys,)
            label = ", ".join(f"{c}={v}" for c, v in zip(series_cols, label_keys, strict=True))
            ax.plot(sub[xcol], sub[metric.column], marker="o", markersize=3, label=label)
        ax.legend()
    else:
        ax.plot(df[xcol], df[metric.column], marker="o", markersize=3)

    apply_log2_axis(ax.xaxis, df[xcol].unique())
    ax.set_xlabel(xcol)
    ax.set_ylabel(metric.label)
    ax.set_ylim(bottom=0, top=args.ymax)
    bench = df["benchmark"].iloc[0] if "benchmark" in df.columns else "ferret"
    ax.set_title(f"{bench}: {metric.label} vs {xcol}")
    ax.grid(True, which="both", linestyle="--", alpha=0.4)

    if args.out:
        fig.savefig(args.out, bbox_inches="tight", dpi=400)
    else:
        plt.show()
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

### Step 1.10: Verify plot.py behavior is unchanged

Re-run the same invocation that produced the pre-flight baseline:

```bash
python3 scripts/plot.py /tmp/ferret-plot-baseline.csv --out=/tmp/ferret-plot-task1.png
file /tmp/ferret-plot-task1.png
```

Expected: `/tmp/ferret-plot-task1.png` is a PNG image. The pixel-exact diff against the pre-flight baseline doesn't need to be byte-identical (matplotlib reorders unordered ops in some versions) but the file must exist and be a valid PNG.

### Step 1.11: Extend ruff to `tests/python/`

- [ ] Edit `scripts/lint.sh`, lines 25 and 28 — replace `scripts/` with `scripts/ tests/python/`:

```bash
echo "==> ruff format --check"
ruff format --check scripts/ tests/python/

echo "==> ruff check"
ruff check scripts/ tests/python/
```

- [ ] Edit `scripts/format.sh`, lines 21-22 — same replacement:

```bash
# Python: format then autofix lint findings.
ruff format scripts/ tests/python/
ruff check --fix scripts/ tests/python/
```

### Step 1.12: Run ruff and the test suite

```bash
scripts/format.sh
scripts/lint.sh
scripts/test_py.sh -q
```

Expected: ruff prints no violations; pytest reports the same 14 passing tests as Step 1.8.

### Step 1.13: Commit

```bash
git add scripts/ferret_plot/ scripts/test_py.sh scripts/plot.py scripts/lint.sh scripts/format.sh tests/python/
git status -s
git commit --no-gpg-sign -m "refactor(plot): extract metadata + metric resolution into ferret_plot.columns"
```

Expected `git status -s` before commit shows new files for the entire `scripts/ferret_plot/` tree, `scripts/test_py.sh`, `tests/python/{conftest.py,fixtures.py,test_columns.py}`, plus modifications to `scripts/plot.py`, `scripts/lint.sh`, `scripts/format.sh`. No other files.

---

## Task 2: `refactor(plot): introduce ferret_plot.registry with per-benchmark defaults`

**Goal of commit:** Add the per-benchmark defaults registry. Switch `scripts/plot.py` to consult the registry for the default X column (replaces the column-order-dependent heuristic). Behavior unchanged on `dependent_chain_throughput` and `direct_branch_footprint`; explicit hint replaces implicit ordering.

**Files:**
- Create: `scripts/ferret_plot/registry.py`
- Create: `tests/python/test_registry.py`
- Modify: `scripts/plot.py` (call `resolve_defaults` for the default X)

### Step 2.1: Write the failing test

- [ ] Create `tests/python/test_registry.py`:

```python
"""Tests for ferret_plot.registry."""

from __future__ import annotations

import pandas as pd
import pytest

from ferret_plot import registry as reg
from ferret_plot.errors import PlotError
from fixtures import dbf_df, dct_df


class TestDetectBenchmark:
    def test_override_wins(self):
        df = dbf_df()
        assert reg.detect_benchmark(df, override="anything") == "anything"

    def test_reads_first_row_when_column_present(self):
        df = dbf_df()
        assert reg.detect_benchmark(df, override=None) == "direct_branch_footprint"

    def test_returns_none_when_column_absent(self):
        df = dbf_df().drop(columns=["benchmark"])
        assert reg.detect_benchmark(df, override=None) is None

    def test_returns_none_for_empty_frame(self):
        df = pd.DataFrame({"benchmark": []})
        assert reg.detect_benchmark(df, override=None) is None

    def test_mixed_benchmark_raises(self):
        df = pd.concat([dbf_df(), dct_df()], ignore_index=True)
        with pytest.raises(PlotError) as exc:
            reg.detect_benchmark(df, override=None)
        msg = str(exc.value)
        assert "direct_branch_footprint" in msg
        assert "dependent_chain_throughput" in msg


class TestResolveDefaults:
    def test_dbf_defaults(self):
        d = reg.resolve_defaults(dbf_df(), override=None)
        assert d.line_x == "branches"
        assert d.heatmap_x == "branches"
        assert d.heatmap_y == "spacing_bytes"

    def test_dct_defaults(self):
        d = reg.resolve_defaults(dct_df(), override=None)
        assert d.line_x == "chain_length"

    def test_unknown_benchmark_returns_empty_defaults(self):
        df = dct_df()
        df["benchmark"] = "imaginary_benchmark"
        d = reg.resolve_defaults(df, override=None)
        assert d.line_x is None
        assert d.heatmap_x is None
        assert d.heatmap_y is None


class TestRegistryHonesty:
    """Every column referenced in DEFAULTS must exist in its benchmark fixture."""

    def test_dbf_columns_present(self):
        df = dbf_df()
        d = reg.DEFAULTS["direct_branch_footprint"]
        for attr in ("line_x", "heatmap_x", "heatmap_y", "facet_col"):
            col = getattr(d, attr)
            if col is not None:
                assert col in df.columns, f"{attr}={col!r} not in dbf_df columns"

    def test_dct_columns_present(self):
        df = dct_df()
        d = reg.DEFAULTS["dependent_chain_throughput"]
        for attr in ("line_x", "heatmap_x", "heatmap_y", "facet_col"):
            col = getattr(d, attr)
            if col is not None:
                assert col in df.columns, f"{attr}={col!r} not in dct_df columns"
```

### Step 2.2: Verify the tests fail

Run:

```bash
scripts/test_py.sh tests/python/test_registry.py -q 2>&1 | tail -10
```

Expected: `ModuleNotFoundError: No module named 'ferret_plot.registry'`.

### Step 2.3: Create `ferret_plot/registry.py`

- [ ] Create `scripts/ferret_plot/registry.py`:

```python
"""Per-benchmark default X/Y/facet column hints.

Keyed off the `benchmark` value in CSV row 0 (or an explicit
--benchmark=NAME override). Lookup misses fall through to ordering
rules in the calling kind; that is not an error.
"""

from __future__ import annotations

from dataclasses import dataclass

import pandas as pd

from ferret_plot.errors import PlotError


@dataclass(frozen=True)
class BenchmarkDefaults:
    line_x: str | None = None
    heatmap_x: str | None = None
    heatmap_y: str | None = None
    facet_col: str | None = None


DEFAULTS: dict[str, BenchmarkDefaults] = {
    "direct_branch_footprint": BenchmarkDefaults(
        line_x="branches",
        heatmap_x="branches",
        heatmap_y="spacing_bytes",
    ),
    "dependent_chain_throughput": BenchmarkDefaults(
        line_x="chain_length",
    ),
}


def detect_benchmark(df: pd.DataFrame, override: str | None) -> str | None:
    """Return the benchmark-name key to look up in DEFAULTS, or None.

    Precedence:
      1. override (from --benchmark=NAME) wins outright.
      2. df['benchmark'].iloc[0] if the column exists and the frame is non-empty.
      3. None if the benchmark column is absent or the frame is empty.

    Raises PlotError if df['benchmark'] contains more than one distinct value.
    """
    if override is not None:
        return override
    if "benchmark" not in df.columns or len(df) == 0:
        return None
    distinct = df["benchmark"].dropna().unique().tolist()
    if len(distinct) > 1:
        raise PlotError(
            f"mixed-benchmark CSV: found {distinct!r}; "
            "pre-filter or pass --benchmark=NAME to force a registry lookup"
        )
    if len(distinct) == 0:
        return None
    return str(distinct[0])


def resolve_defaults(df: pd.DataFrame, override: str | None) -> BenchmarkDefaults:
    """Convenience: detect_benchmark + DEFAULTS lookup with empty fallback."""
    name = detect_benchmark(df, override)
    if name is None:
        return BenchmarkDefaults()
    return DEFAULTS.get(name, BenchmarkDefaults())
```

### Step 2.4: Run the registry tests, verify they pass

```bash
scripts/test_py.sh tests/python/test_registry.py -v 2>&1 | tail -20
```

Expected: 9 tests pass across `TestDetectBenchmark`, `TestResolveDefaults`, and `TestRegistryHonesty`.

### Step 2.5: Wire the registry into `scripts/plot.py`

- [ ] Edit `scripts/plot.py`. Add `resolve_defaults` to the imports block (around line 25):

```python
from ferret_plot.columns import resolve_metric, varying_axis_columns
from ferret_plot.errors import PlotError
from ferret_plot.formatting import apply_log2_axis
from ferret_plot.registry import resolve_defaults
```

- [ ] In `main()`, replace the `xcol` resolution block. Current code (after Task 1):

```python
        df = pd.read_csv(args.csv)
        metric = resolve_metric(df, metric="auto", stat="min")
        xcol = args.x or _auto_x_column(df, metric.column)
```

becomes:

```python
        df = pd.read_csv(args.csv)
        metric = resolve_metric(df, metric="auto", stat="min")
        defaults = resolve_defaults(df, override=None)
        xcol = args.x or defaults.line_x or _auto_x_column(df, metric.column)
```

The `_auto_x_column` helper stays as the fallback for CSVs from benchmarks not in `DEFAULTS`.

### Step 2.6: Verify plot.py still produces the same output

```bash
python3 scripts/plot.py /tmp/ferret-plot-baseline.csv --out=/tmp/ferret-plot-task2.png
file /tmp/ferret-plot-task2.png
```

Expected: `/tmp/ferret-plot-task2.png` is a PNG. For `direct_branch_footprint`, the registry's `line_x="branches"` happens to match what `_auto_x_column` already picked (column order is `branches, spacing_bytes`), so the plot is the same shape.

### Step 2.7: Run the full test suite

```bash
scripts/test_py.sh -q
scripts/lint.sh
```

Expected: all Task 1 + Task 2 tests green; ruff clean.

### Step 2.8: Commit

```bash
git add scripts/ferret_plot/registry.py tests/python/test_registry.py scripts/plot.py
git status -s
git commit --no-gpg-sign -m "refactor(plot): introduce ferret_plot.registry with per-benchmark defaults"
```

---

## Task 3: `feat(plot): subcommand CLI with line kind`

**Goal of commit:** Promote `scripts/plot.py` to a thin entry shim. Introduce `ferret_plot.cli` with argparse subparsers and `ferret_plot.kinds.line`. The user-visible CLI changes from `plot.py FILE.csv` to `plot.py line FILE.csv`. README updated.

**Files:**
- Create: `scripts/ferret_plot/cli.py`
- Create: `scripts/ferret_plot/kinds/__init__.py`
- Create: `scripts/ferret_plot/kinds/line.py`
- Create: `tests/python/test_line.py`
- Create: `tests/python/test_cli.py`
- Modify: `scripts/plot.py` (replace with shim)
- Modify: `README.md` (update Step 3 of the workflow + add spacing_bytes example)

### Step 3.1: Write the failing test for the line kind

- [ ] Create `tests/python/test_line.py`:

```python
"""Tests for ferret_plot.kinds.line."""

from __future__ import annotations

import argparse

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt

from ferret_plot.kinds import line as line_kind
from fixtures import dbf_df, dct_df


def _args(**overrides):
    """Mimic the argparse.Namespace the CLI hands to make_figure."""
    base = {
        "csv": "",
        "out": None,
        "benchmark": None,
        "metric": "auto",
        "stat": "min",
        "x": None,
        "series": None,
        "ymax": None,
    }
    base.update(overrides)
    return argparse.Namespace(**base)


class TestLineMakeFigure:
    def teardown_method(self):
        plt.close("all")

    def test_dbf_default_x_is_branches(self):
        df = dbf_df()
        fig = line_kind.make_figure(df, _args())
        ax = fig.axes[0]
        assert ax.get_xlabel() == "branches"
        # One line per unique spacing value.
        assert len(ax.get_lines()) == df["spacing_bytes"].nunique()

    def test_dbf_explicit_x_spacing_bytes(self):
        df = dbf_df()
        fig = line_kind.make_figure(df, _args(x="spacing_bytes"))
        ax = fig.axes[0]
        assert ax.get_xlabel() == "spacing_bytes"
        # One line per unique branches value.
        assert len(ax.get_lines()) == df["branches"].nunique()

    def test_dct_single_axis_single_line(self):
        df = dct_df(chain_lengths=(10**6, 10**7, 10**8))
        fig = line_kind.make_figure(df, _args())
        ax = fig.axes[0]
        assert ax.get_xlabel() == "chain_length"
        assert len(ax.get_lines()) == 1

    def test_ylabel_reflects_metric(self):
        df = dbf_df(with_freq=True)
        fig = line_kind.make_figure(df, _args(metric="cycles", stat="min"))
        ax = fig.axes[0]
        assert "cycles per site" in ax.get_ylabel()

    def test_ymax_caps_y_limit(self):
        df = dbf_df()
        fig = line_kind.make_figure(df, _args(ymax=10.0))
        ax = fig.axes[0]
        assert ax.get_ylim()[1] == 10.0

    def test_series_filter_pins_grouping(self):
        df = dbf_df()
        # Pin series to spacing_bytes only; the other varying axis (branches)
        # would normally also become a series — but with --series=spacing_bytes
        # the line count should match unique spacing values.
        fig = line_kind.make_figure(df, _args(x="branches", series="spacing_bytes"))
        ax = fig.axes[0]
        assert len(ax.get_lines()) == df["spacing_bytes"].nunique()
```

### Step 3.2: Write the failing CLI test

- [ ] Create `tests/python/test_cli.py`:

```python
"""End-to-end CLI tests for ferret_plot.cli."""

from __future__ import annotations

from pathlib import Path

import pytest

from ferret_plot import cli
from fixtures import dbf_df, dct_df


def _write_csv(df, tmp_path: Path, name: str) -> str:
    p = tmp_path / name
    df.to_csv(p, index=False)
    return str(p)


class TestLineSubcommand:
    def test_line_produces_png(self, tmp_path):
        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "out.png"
        rc = cli.main(["line", csv_path, "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()
        assert out_path.stat().st_size > 0
        # PNG magic bytes.
        assert out_path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"

    def test_line_with_spacing_bytes_x(self, tmp_path):
        csv_path = _write_csv(dbf_df(), tmp_path, "btb.csv")
        out_path = tmp_path / "out.png"
        rc = cli.main(["line", csv_path, "--x", "spacing_bytes", "--out", str(out_path)])
        assert rc == 0
        assert out_path.exists()


class TestErrorPaths:
    def test_mixed_benchmark_csv_exits_2(self, tmp_path, capsys):
        import pandas as pd
        mixed = pd.concat([dbf_df(), dct_df()], ignore_index=True)
        csv_path = _write_csv(mixed, tmp_path, "mixed.csv")
        rc = cli.main(["line", csv_path, "--out", str(tmp_path / "x.png")])
        assert rc == 2
        err = capsys.readouterr().err
        assert "mixed-benchmark CSV" in err

    def test_missing_subcommand_exits_nonzero(self, tmp_path):
        with pytest.raises(SystemExit) as exc:
            cli.main([])  # no subcommand
        assert exc.value.code != 0
```

### Step 3.3: Verify the tests fail

```bash
scripts/test_py.sh tests/python/test_line.py tests/python/test_cli.py -q 2>&1 | tail -10
```

Expected: `ModuleNotFoundError: No module named 'ferret_plot.kinds'` (or `.cli`).

### Step 3.4: Create the kinds package skeleton

- [ ] Create `scripts/ferret_plot/kinds/__init__.py`:

```python
"""Plot-kind renderers: one module per CLI subcommand (`line`, `heatmap`, `facets`).

Each kind exposes `make_figure(df, args) -> matplotlib.figure.Figure`,
where args is the argparse.Namespace produced by ferret_plot.cli.
"""
```

### Step 3.5: Implement `kinds/line.py`

- [ ] Create `scripts/ferret_plot/kinds/line.py`:

```python
"""Line+series plot. Same visual output as the original plot.py for 1-axis CSVs."""

from __future__ import annotations

import argparse

import matplotlib.pyplot as plt
import pandas as pd
from matplotlib.figure import Figure

from ferret_plot.columns import resolve_metric, varying_axis_columns
from ferret_plot.errors import PlotError
from ferret_plot.formatting import apply_log2_axis
from ferret_plot.registry import resolve_defaults


def _resolve_x(df: pd.DataFrame, args: argparse.Namespace) -> str:
    if args.x is not None:
        if args.x not in df.columns:
            raise PlotError(f"--x={args.x!r} is not a column in the CSV")
        return args.x
    defaults = resolve_defaults(df, override=args.benchmark)
    if defaults.line_x is not None and defaults.line_x in df.columns:
        return defaults.line_x
    for col in varying_axis_columns(df):
        return col
    raise PlotError("no varying axis column to use as X")


def _resolve_series(df: pd.DataFrame, args: argparse.Namespace, xcol: str) -> list[str]:
    if args.series is not None:
        cols = [c.strip() for c in args.series.split(",") if c.strip()]
        for c in cols:
            if c not in df.columns:
                raise PlotError(f"--series column {c!r} is not a column in the CSV")
        return cols
    return [c for c in varying_axis_columns(df) if c != xcol]


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    xcol = _resolve_x(df, args)
    series_cols = _resolve_series(df, args, xcol)

    fig, ax = plt.subplots(figsize=(8, 5))
    if series_cols:
        for keys, sub in df.groupby(series_cols):
            label_keys = keys if isinstance(keys, tuple) else (keys,)
            label = ", ".join(f"{c}={v}" for c, v in zip(series_cols, label_keys, strict=True))
            ax.plot(sub[xcol], sub[metric.column], marker="o", markersize=3, label=label)
        ax.legend()
    else:
        ax.plot(df[xcol], df[metric.column], marker="o", markersize=3)

    apply_log2_axis(ax.xaxis, df[xcol].unique())
    ax.set_xlabel(xcol)
    ax.set_ylabel(metric.label)
    ax.set_ylim(bottom=0, top=args.ymax)
    bench = df["benchmark"].iloc[0] if "benchmark" in df.columns and len(df) else "ferret"
    ax.set_title(f"{bench}: {metric.label} vs {xcol}")
    ax.grid(True, which="both", linestyle="--", alpha=0.4)
    return fig
```

### Step 3.6: Implement `ferret_plot/cli.py`

- [ ] Create `scripts/ferret_plot/cli.py`:

```python
"""argparse subparsers and dispatch for the plot script.

main(argv) is the entry point; argv defaults to sys.argv[1:].
Each subcommand resolves to a kind module's make_figure().
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
from ferret_plot.kinds import line as line_kind


def _add_common(sp: argparse.ArgumentParser) -> None:
    sp.add_argument("csv")
    sp.add_argument("--out", default=None,
                    help="output image path; omitted = plt.show()")
    sp.add_argument("--benchmark", default=None,
                    help="override registry lookup (rare)")
    sp.add_argument("--metric", default="auto",
                    choices=["auto", "cycles", "ns"])
    sp.add_argument("--stat", default="min",
                    choices=["min", "median"])


def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(
        prog="plot.py",
        description="Plot a ferret CSV as line, heatmap, or facet grid.",
    )
    sub = ap.add_subparsers(dest="kind", required=True)

    line = sub.add_parser("line", help="line plot with series fan-out")
    _add_common(line)
    line.add_argument("--x", default=None, help="X-axis column")
    line.add_argument("--series", default=None,
                      help="comma-separated columns to use as series grouping")
    line.add_argument("--ymax", type=float, default=None,
                      help="upper limit for the Y axis")

    return ap


_DISPATCH = {
    "line": line_kind.make_figure,
}


def _emit(fig: Figure, out: str | None) -> None:
    if out:
        fig.savefig(out, bbox_inches="tight", dpi=400)
    else:
        plt.show()


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        df = pd.read_csv(args.csv)
        fig = _DISPATCH[args.kind](df, args)
        _emit(fig, args.out)
        return 0
    except PlotError as e:
        print(f"plot.py: {e}", file=sys.stderr)
        return 2
```

### Step 3.7: Replace `scripts/plot.py` with the entry shim

- [ ] Overwrite `scripts/plot.py`:

```python
#!/usr/bin/env python3
"""Entry shim. The real CLI lives in scripts/ferret_plot/cli.py."""

from __future__ import annotations

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from ferret_plot.cli import main

if __name__ == "__main__":
    sys.exit(main())
```

### Step 3.8: Run the tests, verify they pass

```bash
scripts/test_py.sh -v 2>&1 | tail -30
```

Expected: every Task 1 / Task 2 / Task 3 test passes. Total around 30 test methods. `TestLineMakeFigure`, `TestLineSubcommand`, `TestErrorPaths` all green.

### Step 3.9: End-to-end sanity check via the new CLI

```bash
python3 scripts/plot.py line /tmp/ferret-plot-baseline.csv --out=/tmp/ferret-plot-task3.png
file /tmp/ferret-plot-task3.png
python3 scripts/plot.py line /tmp/ferret-plot-baseline.csv --x=spacing_bytes --out=/tmp/ferret-plot-task3-by-spacing.png
file /tmp/ferret-plot-task3-by-spacing.png
```

Expected: both PNGs exist. The second has `spacing_bytes` on X.

### Step 3.10: Update the README

- [ ] Edit `README.md`, line 94-95 (Step 3 of "The two-step cycle workflow"). Current:

```sh
# Step 3: plot — picks cycles_per_site automatically because freq was set.
python3 scripts/plot.py /tmp/btb.csv --out=/tmp/btb.png
```

becomes:

```sh
# Step 3: line plot — picks cycles_per_site automatically because freq was set.
python3 scripts/plot.py line /tmp/btb.csv --out=/tmp/btb.png

# Or with spacing_bytes on X (branches becomes the legend):
python3 scripts/plot.py line /tmp/btb.csv --x=spacing_bytes --out=/tmp/btb-by-spacing.png
```

### Step 3.11: Run lint + tests

```bash
scripts/format.sh
scripts/lint.sh
scripts/test_py.sh -q
```

Expected: clean ruff, all tests pass.

### Step 3.12: Commit

```bash
git add scripts/ferret_plot/cli.py scripts/ferret_plot/kinds/ scripts/plot.py tests/python/test_line.py tests/python/test_cli.py README.md
git status -s
git commit --no-gpg-sign -m "feat(plot): subcommand CLI with line kind"
```

---

## Task 4: `feat(plot): heatmap kind for 2-axis benchmarks`

**Goal of commit:** Add `kinds/heatmap.py` and wire the `heatmap` subparser. Two-axis CSVs (today's `direct_branch_footprint`) render as a 2D heatmap with a colorbar. README example added.

**Files:**
- Create: `scripts/ferret_plot/kinds/heatmap.py`
- Create: `tests/python/test_heatmap.py`
- Modify: `scripts/ferret_plot/cli.py` (add `heatmap` subparser + dispatch entry)
- Modify: `README.md` (add heatmap example next to the line example)

### Step 4.1: Write the failing test

- [ ] Create `tests/python/test_heatmap.py`:

```python
"""Tests for ferret_plot.kinds.heatmap."""

from __future__ import annotations

import argparse

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pytest
from matplotlib.colors import LogNorm, Normalize

from ferret_plot.errors import PlotError
from ferret_plot.kinds import heatmap as heatmap_kind
from fixtures import dbf_df, dct_df


def _args(**overrides):
    base = {
        "csv": "",
        "out": None,
        "benchmark": None,
        "metric": "auto",
        "stat": "min",
        "x": None,
        "y": None,
        "logz": False,
    }
    base.update(overrides)
    return argparse.Namespace(**base)


def _imshow_artist(ax):
    """Return the AxesImage that imshow places on ax."""
    for im in ax.get_images():
        return im
    raise AssertionError("no AxesImage on Axes")


class TestHeatmapMakeFigure:
    def teardown_method(self):
        plt.close("all")

    def test_dbf_default_axes(self):
        branches = (1, 2, 4, 8)
        spacing = (16, 32, 64)
        df = dbf_df(branches=branches, spacing=spacing)
        fig = heatmap_kind.make_figure(df, _args())
        ax = fig.axes[0]
        im = _imshow_artist(ax)
        # imshow data has shape (rows=Y, cols=X) = (len(spacing), len(branches)).
        assert im.get_array().shape == (len(spacing), len(branches))
        assert ax.get_xlabel() == "branches"
        assert ax.get_ylabel() == "spacing_bytes"

    def test_explicit_x_transposes(self):
        branches = (1, 2, 4, 8)
        spacing = (16, 32, 64)
        df = dbf_df(branches=branches, spacing=spacing)
        fig = heatmap_kind.make_figure(df, _args(x="spacing_bytes", y="branches"))
        ax = fig.axes[0]
        im = _imshow_artist(ax)
        assert im.get_array().shape == (len(branches), len(spacing))
        assert ax.get_xlabel() == "spacing_bytes"
        assert ax.get_ylabel() == "branches"

    def test_logz_sets_lognorm(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args(logz=True))
        im = _imshow_artist(fig.axes[0])
        assert isinstance(im.norm, LogNorm)

    def test_default_norm_is_linear(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args())
        im = _imshow_artist(fig.axes[0])
        assert isinstance(im.norm, Normalize) and not isinstance(im.norm, LogNorm)

    def test_one_axis_csv_raises(self):
        df = dct_df()
        with pytest.raises(PlotError) as exc:
            heatmap_kind.make_figure(df, _args())
        assert "fewer than 2" in str(exc.value)

    def test_colorbar_present(self):
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args())
        # imshow + colorbar => two Axes (main + colorbar).
        assert len(fig.axes) == 2

    def test_explicit_x_only_swaps_registry_y(self):
        # Regression: with DBF's registry (heatmap_y='spacing_bytes'), passing
        # --x=spacing_bytes alone must not pick the same column for Y.
        # The renderer should fall through to the remaining varying axis (branches).
        df = dbf_df()
        fig = heatmap_kind.make_figure(df, _args(x="spacing_bytes"))
        ax = fig.axes[0]
        assert ax.get_xlabel() == "spacing_bytes"
        assert ax.get_ylabel() == "branches"
```

### Step 4.2: Verify the tests fail

```bash
scripts/test_py.sh tests/python/test_heatmap.py -q 2>&1 | tail -10
```

Expected: `ModuleNotFoundError: No module named 'ferret_plot.kinds.heatmap'`.

### Step 4.3: Implement `kinds/heatmap.py`

- [ ] Create `scripts/ferret_plot/kinds/heatmap.py`:

```python
"""2D heatmap renderer for CSVs with at least 2 varying axis columns."""

from __future__ import annotations

import argparse

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.colors import LogNorm, Normalize
from matplotlib.figure import Figure

from ferret_plot.columns import resolve_metric, varying_axis_columns
from ferret_plot.errors import PlotError
from ferret_plot.formatting import human_readable
from ferret_plot.registry import resolve_defaults


def _resolve_xy(df: pd.DataFrame, args: argparse.Namespace) -> tuple[str, str]:
    varying = varying_axis_columns(df)
    if len(varying) < 2:
        raise PlotError(
            f"heatmap needs at least 2 varying axis columns; "
            f"this CSV has {len(varying)}: {varying!r}"
        )
    defaults = resolve_defaults(df, override=args.benchmark)

    x = args.x
    if x is None and defaults.heatmap_x is not None and defaults.heatmap_x in df.columns:
        x = defaults.heatmap_x
    if x is None:
        x = varying[0]
    if x not in df.columns:
        raise PlotError(f"--x={x!r} is not a column in the CSV")

    y = args.y
    # Skip the registry's Y suggestion if it collides with X (e.g. user
    # passed --x=spacing_bytes on a CSV whose registry default Y is also
    # spacing_bytes). Fall through to "first varying column that isn't X".
    if y is None and defaults.heatmap_y is not None and defaults.heatmap_y in df.columns and defaults.heatmap_y != x:
        y = defaults.heatmap_y
    if y is None:
        y = next((c for c in varying if c != x), None)
    if y is None:
        raise PlotError("could not pick a Y axis (only one varying axis after X)")
    if y not in df.columns:
        raise PlotError(f"--y={y!r} is not a column in the CSV")
    if x == y:
        raise PlotError(f"X and Y must differ (both = {x!r})")
    return x, y


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    xcol, ycol = _resolve_xy(df, args)

    # pivot_table(aggfunc='first') because each (X, Y) cell corresponds to
    # one ferret row. Duplicates from concatenated CSVs would silently
    # average with the default aggfunc; 'first' keeps the data faithful.
    pivot = df.pivot_table(
        index=ycol, columns=xcol, values=metric.column, aggfunc="first",
    )
    pivot = pivot.sort_index().sort_index(axis=1)
    data = np.ma.masked_invalid(pivot.values)

    fig, ax = plt.subplots(figsize=(8, 5))
    norm = LogNorm() if args.logz else Normalize()
    im = ax.imshow(data, aspect="auto", origin="lower", norm=norm, cmap="viridis")
    # Mark NaN cells with a hatched overlay so absence is visible.
    im.cmap.set_bad(color="lightgrey")

    ax.set_xticks(range(len(pivot.columns)))
    ax.set_xticklabels([human_readable(v) for v in pivot.columns])
    ax.set_yticks(range(len(pivot.index)))
    ax.set_yticklabels([human_readable(v) for v in pivot.index])
    ax.set_xlabel(xcol)
    ax.set_ylabel(ycol)
    bench = df["benchmark"].iloc[0] if "benchmark" in df.columns and len(df) else "ferret"
    ax.set_title(f"{bench}: {metric.label} ({ycol} × {xcol})")

    cbar = fig.colorbar(im, ax=ax)
    cbar.set_label(metric.label)
    return fig
```

### Step 4.4: Wire the heatmap subparser into `cli.py`

- [ ] Edit `scripts/ferret_plot/cli.py`. Add the import:

```python
from ferret_plot.kinds import heatmap as heatmap_kind
from ferret_plot.kinds import line as line_kind
```

- [ ] Inside `build_parser()`, after the `line` subparser block, add:

```python
    heat = sub.add_parser("heatmap", help="2D heatmap over two varying axes")
    _add_common(heat)
    heat.add_argument("--x", default=None, help="X-axis column")
    heat.add_argument("--y", default=None, help="Y-axis column")
    heat.add_argument("--logz", action="store_true", help="log color scale")
```

- [ ] Extend `_DISPATCH`:

```python
_DISPATCH = {
    "line": line_kind.make_figure,
    "heatmap": heatmap_kind.make_figure,
}
```

### Step 4.5: Run the tests

```bash
scripts/test_py.sh -q 2>&1 | tail -10
```

Expected: every existing test plus the 6 new `TestHeatmapMakeFigure` tests pass.

### Step 4.6: End-to-end CLI smoke test

```bash
python3 scripts/plot.py heatmap /tmp/ferret-plot-baseline.csv --out=/tmp/ferret-plot-heatmap.png
file /tmp/ferret-plot-heatmap.png
```

Expected: PNG. The baseline CSV has `--branches=1,2,4,8 --spacing_bytes=64` (one spacing value) which would normally trigger the `fewer than 2 varying` error — re-generate with a spacing sweep:

```bash
build/ferret run direct_branch_footprint --branches=1..16 --spacing_bytes=16,32,64 \
  --reps=3 --warmup=1 --seed=1 --freq=4.0GHz --out=/tmp/ferret-plot-2axis.csv
python3 scripts/plot.py heatmap /tmp/ferret-plot-2axis.csv --out=/tmp/ferret-plot-heatmap.png
file /tmp/ferret-plot-heatmap.png
```

Expected: valid PNG.

### Step 4.7: Update the README

- [ ] Edit `README.md`, lines 94-99 (the section updated in Task 3). Replace the two-line block from Task 3:

```sh
# Step 3: line plot — picks cycles_per_site automatically because freq was set.
python3 scripts/plot.py line /tmp/btb.csv --out=/tmp/btb.png

# Or with spacing_bytes on X (branches becomes the legend):
python3 scripts/plot.py line /tmp/btb.csv --x=spacing_bytes --out=/tmp/btb-by-spacing.png
```

with:

```sh
# Step 3: line plot — picks cycles_per_site automatically because freq was set.
python3 scripts/plot.py line /tmp/btb.csv --out=/tmp/btb.png

# Or with spacing_bytes on X (branches becomes the legend):
python3 scripts/plot.py line /tmp/btb.csv --x=spacing_bytes --out=/tmp/btb-by-spacing.png

# Or as a 2D heatmap (branches × spacing_bytes, cycles per site as color):
python3 scripts/plot.py heatmap /tmp/btb.csv --out=/tmp/btb-heatmap.png
```

### Step 4.8: Lint + tests

```bash
scripts/format.sh
scripts/lint.sh
scripts/test_py.sh -q
```

Expected: clean.

### Step 4.9: Commit

```bash
git add scripts/ferret_plot/kinds/heatmap.py scripts/ferret_plot/cli.py tests/python/test_heatmap.py README.md
git status -s
git commit --no-gpg-sign -m "feat(plot): heatmap kind for 2-axis benchmarks"
```

---

## Task 5: `feat(plot): facets kind for >=3-axis sweeps`

**Goal of commit:** Add `kinds/facets.py` for grid-of-heatmaps. Required `--facet=COL` argument with optional registry fallback. Subplots share a single colorbar and color scale. README gets the "Plot subcommands" subsection.

**Files:**
- Create: `scripts/ferret_plot/kinds/facets.py`
- Create: `tests/python/test_facets.py`
- Modify: `tests/python/fixtures.py` (add `three_axis_df`)
- Modify: `scripts/ferret_plot/cli.py` (add `facets` subparser + dispatch)
- Modify: `README.md` (add the "Plot subcommands" subsection)

### Step 5.1: Extend `fixtures.py` with a 3-axis builder

- [ ] Append to `tests/python/fixtures.py`:

```python
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
                    row["cycles_per_site_min"] = ns * 4.0
                    row["cycles_per_site_median"] = ns * 4.0
                    row["freq_hz"] = 4.0e9
                rows.append(row)
    return pd.DataFrame(rows)
```

### Step 5.2: Write the failing test for facets

- [ ] Create `tests/python/test_facets.py`:

```python
"""Tests for ferret_plot.kinds.facets."""

from __future__ import annotations

import argparse

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt
import pytest

from ferret_plot.errors import PlotError
from ferret_plot.kinds import facets as facets_kind
from fixtures import dbf_df, three_axis_df


def _args(**overrides):
    base = {
        "csv": "",
        "out": None,
        "benchmark": None,
        "metric": "auto",
        "stat": "min",
        "facet": None,
        "x": None,
        "y": None,
        "logz": False,
    }
    base.update(overrides)
    return argparse.Namespace(**base)


def _heatmap_axes(fig):
    """Return only the Axes that contain an AxesImage (heatmap subplots),
    excluding the shared colorbar axis."""
    return [ax for ax in fig.axes if ax.get_images()]


class TestFacetsMakeFigure:
    def teardown_method(self):
        plt.close("all")

    def test_one_subplot_per_facet_value(self):
        df = three_axis_df(variants=("a", "b", "c"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        assert len(_heatmap_axes(fig)) == 3

    def test_shared_color_scale_across_subplots(self):
        df = three_axis_df(variants=("a", "b"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        ax_list = _heatmap_axes(fig)
        norms = [ax.get_images()[0].norm for ax in ax_list]
        assert all(n.vmin == norms[0].vmin for n in norms)
        assert all(n.vmax == norms[0].vmax for n in norms)

    def test_shared_colorbar(self):
        df = three_axis_df(variants=("a", "b"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        # heatmap subplots + 1 shared colorbar = N+1 axes total.
        assert len(fig.axes) == len(_heatmap_axes(fig)) + 1

    def test_missing_facet_raises_when_no_registry_default(self):
        df = three_axis_df()
        with pytest.raises(PlotError) as exc:
            facets_kind.make_figure(df, _args(facet=None))
        assert "--facet" in str(exc.value)

    def test_two_axis_csv_raises(self):
        df = dbf_df()
        with pytest.raises(PlotError) as exc:
            facets_kind.make_figure(df, _args(facet="spacing_bytes"))
        assert "needs at least 3" in str(exc.value)

    def test_subplot_title_includes_facet_value(self):
        df = three_axis_df(variants=("a", "b"))
        fig = facets_kind.make_figure(df, _args(facet="variant"))
        titles = [ax.get_title() for ax in _heatmap_axes(fig)]
        assert any("variant=a" in t for t in titles)
        assert any("variant=b" in t for t in titles)
```

### Step 5.3: Verify the tests fail

```bash
scripts/test_py.sh tests/python/test_facets.py -q 2>&1 | tail -10
```

Expected: `ModuleNotFoundError: No module named 'ferret_plot.kinds.facets'`.

### Step 5.4: Implement `kinds/facets.py`

- [ ] Create `scripts/ferret_plot/kinds/facets.py`:

```python
"""Grid-of-heatmaps renderer for CSVs with at least 3 varying axes."""

from __future__ import annotations

import argparse
import math

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from matplotlib.colors import LogNorm, Normalize
from matplotlib.figure import Figure

from ferret_plot.columns import resolve_metric, varying_axis_columns
from ferret_plot.errors import PlotError
from ferret_plot.formatting import human_readable
from ferret_plot.registry import resolve_defaults


def _resolve_facet(df: pd.DataFrame, args: argparse.Namespace) -> str:
    if args.facet is not None:
        if args.facet not in df.columns:
            raise PlotError(f"--facet={args.facet!r} is not a column in the CSV")
        return args.facet
    defaults = resolve_defaults(df, override=args.benchmark)
    if defaults.facet_col is not None and defaults.facet_col in df.columns:
        return defaults.facet_col
    raise PlotError("--facet=COL is required (and no registry default applies)")


def _resolve_xy(df: pd.DataFrame, args: argparse.Namespace, facet: str) -> tuple[str, str]:
    remaining = [c for c in varying_axis_columns(df) if c != facet]
    if len(remaining) < 2:
        raise PlotError(
            f"facets needs at least 3 varying axes total; "
            f"after factoring out --facet={facet!r}, only {remaining!r} remain"
        )
    defaults = resolve_defaults(df, override=args.benchmark)
    x = args.x or (defaults.heatmap_x if defaults.heatmap_x in remaining else None) or remaining[0]
    y = args.y or (defaults.heatmap_y if defaults.heatmap_y in remaining and defaults.heatmap_y != x else None)
    if y is None:
        y = next((c for c in remaining if c != x), None)
    if y is None or x == y:
        raise PlotError(f"could not pick distinct X/Y after facet (got x={x!r}, y={y!r})")
    return x, y


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    facet = _resolve_facet(df, args)
    xcol, ycol = _resolve_xy(df, args, facet)

    facet_values = sorted(df[facet].dropna().unique().tolist())
    n = len(facet_values)
    ncols = max(1, math.ceil(math.sqrt(n)))
    nrows = math.ceil(n / ncols)

    # Shared color scale across all subplots.
    vmin = float(np.nanmin(df[metric.column].to_numpy()))
    vmax = float(np.nanmax(df[metric.column].to_numpy()))
    norm = LogNorm(vmin=vmin, vmax=vmax) if args.logz else Normalize(vmin=vmin, vmax=vmax)

    fig, axes = plt.subplots(nrows, ncols, figsize=(4 * ncols, 3.5 * nrows), squeeze=False)
    last_im = None
    for idx, value in enumerate(facet_values):
        ax = axes[idx // ncols][idx % ncols]
        sub = df[df[facet] == value]
        pivot = sub.pivot_table(
            index=ycol, columns=xcol, values=metric.column, aggfunc="first",
        ).sort_index().sort_index(axis=1)
        data = np.ma.masked_invalid(pivot.values)
        im = ax.imshow(data, aspect="auto", origin="lower", norm=norm, cmap="viridis")
        im.cmap.set_bad(color="lightgrey")
        ax.set_xticks(range(len(pivot.columns)))
        ax.set_xticklabels([human_readable(v) for v in pivot.columns])
        ax.set_yticks(range(len(pivot.index)))
        ax.set_yticklabels([human_readable(v) for v in pivot.index])
        ax.set_xlabel(xcol)
        ax.set_ylabel(ycol)
        ax.set_title(f"{facet}={value}")
        last_im = im

    # Blank any unused grid cells.
    for idx in range(n, nrows * ncols):
        axes[idx // ncols][idx % ncols].axis("off")

    bench = df["benchmark"].iloc[0] if "benchmark" in df.columns and len(df) else "ferret"
    fig.suptitle(f"{bench}: {metric.label}")
    cbar = fig.colorbar(last_im, ax=axes.ravel().tolist(), shrink=0.85)
    cbar.set_label(metric.label)
    return fig
```

### Step 5.5: Wire the facets subparser into `cli.py`

- [ ] Edit `scripts/ferret_plot/cli.py`. Add the import:

```python
from ferret_plot.kinds import facets as facets_kind
from ferret_plot.kinds import heatmap as heatmap_kind
from ferret_plot.kinds import line as line_kind
```

- [ ] Inside `build_parser()`, after the `heatmap` subparser block, add:

```python
    fac = sub.add_parser("facets", help="grid of heatmaps over >=3 varying axes")
    _add_common(fac)
    fac.add_argument("--facet", default=None,
                     help="column to facet on (one subplot per unique value)")
    fac.add_argument("--x", default=None, help="X-axis column (per subplot)")
    fac.add_argument("--y", default=None, help="Y-axis column (per subplot)")
    fac.add_argument("--logz", action="store_true", help="log color scale")
```

- [ ] Extend `_DISPATCH`:

```python
_DISPATCH = {
    "line": line_kind.make_figure,
    "heatmap": heatmap_kind.make_figure,
    "facets": facets_kind.make_figure,
}
```

### Step 5.6: Run the tests

```bash
scripts/test_py.sh -q 2>&1 | tail -10
```

Expected: every test from Tasks 1-4 plus the 6 new `TestFacetsMakeFigure` tests pass.

### Step 5.7: End-to-end CLI smoke test

```bash
python3 -c "
import sys, os
sys.path.insert(0, 'tests/python')
from fixtures import three_axis_df
three_axis_df(branches=(1,2,4,8), spacing=(16,32,64), variants=('a','b','c')).to_csv('/tmp/ferret-plot-3axis.csv', index=False)
"
python3 scripts/plot.py facets /tmp/ferret-plot-3axis.csv --facet=variant --out=/tmp/ferret-plot-facets.png
file /tmp/ferret-plot-facets.png
```

Expected: valid PNG with 3 heatmap subplots.

### Step 5.8: Update the README — add the "Plot subcommands" subsection

- [ ] Edit `README.md`. After the existing "Per-benchmark options" section (lines 115-124) and before the "Formatting and linting" section (line 126), insert a new subsection. The literal text to insert (between the outer 4-backtick fences below; the inner triple-backticks are part of what goes into the file):

````markdown
### Plot subcommands

`scripts/plot.py` exposes three rendering kinds; each accepts a CSV
produced by `ferret run`.

```
python3 scripts/plot.py line     FILE.csv [--x=COL] [--out=PATH] [--metric=cycles|ns] [--stat=min|median] [--ymax=N] [--series=COL,...]
python3 scripts/plot.py heatmap  FILE.csv [--x=COL] [--y=COL] [--out=PATH] [--metric=...] [--stat=...] [--logz]
python3 scripts/plot.py facets   FILE.csv --facet=COL [--x=COL] [--y=COL] [--out=PATH] [--metric=...] [--stat=...] [--logz]
```

The plot script reads row 0 of the CSV's `benchmark` column to choose
sensible X/Y defaults per benchmark; pass `--x`/`--y` to override. Use
`facets` with `--facet=COL` when a CSV has three or more varying axes
(future multi-parameter sweeps such as a TAGE capacity test).
````

### Step 5.9: Lint + tests

```bash
scripts/format.sh
scripts/lint.sh
scripts/test_py.sh -q
```

Expected: clean.

### Step 5.10: Commit

```bash
git add scripts/ferret_plot/kinds/facets.py scripts/ferret_plot/cli.py tests/python/test_facets.py tests/python/fixtures.py README.md
git status -s
git commit --no-gpg-sign -m "feat(plot): facets kind for >=3-axis sweeps"
```

---

## Post-flight

- [ ] **Verify the spec's acceptance criteria**

```bash
wc -l scripts/plot.py
find scripts/ferret_plot -name '*.py' -exec wc -l {} +
```

Expected: `scripts/plot.py` ≤ 15 lines. No file in `scripts/ferret_plot/` exceeds ~150 lines.

- [ ] **Equivalence check vs. baseline line plot**

```bash
python3 scripts/plot.py line /tmp/ferret-plot-baseline.csv --out=/tmp/ferret-plot-final.png
file /tmp/ferret-plot-final.png
```

Expected: valid PNG; visually similar shape to the pre-flight `/tmp/ferret-plot-baseline.png`.

- [ ] **C++ tree byte-identity check**

```bash
git diff main --name-only -- '*.cpp' '*.hpp' '**/CMakeLists.txt' CMakeLists.txt
```

Expected: empty output. `tests/python/` (Python only) is the only new tree under `tests/`; the C++ test sources and CMake wiring are unchanged.

- [ ] **freq.py untouched**

```bash
git diff main -- scripts/freq.py
```

Expected: empty diff.

- [ ] **Final test run**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
scripts/test_py.sh -v
scripts/lint.sh
```

Expected: every C++ test passes; every Python test passes; ruff and clang-format/clang-tidy clean.

- [ ] **Commit history sanity**

```bash
git log --oneline main..HEAD
```

Expected: exactly 5 commits with these subjects, in order:
1. `refactor(plot): extract metadata + metric resolution into ferret_plot.columns`
2. `refactor(plot): introduce ferret_plot.registry with per-benchmark defaults`
3. `feat(plot): subcommand CLI with line kind`
4. `feat(plot): heatmap kind for 2-axis benchmarks`
5. `feat(plot): facets kind for >=3-axis sweeps`

# Surface Plot Command Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a static matplotlib 3D `surface` plot subcommand for two-axis capacity sweeps, with Z height and colormap driven by the selected metric.

**Architecture:** Add one focused renderer module under `scripts/ferret_plot/kinds/`, reuse the existing heatmap X/Y resolution rules, and extract shared grid preparation so heatmap and surface pivot data the same way. Keep the CLI static-PNG-only and keep TAGE-specific registry defaults out until benchmark column names exist.

**Tech Stack:** Python 3.11+, pandas, numpy, matplotlib `Agg`, pytest, ruff.

**Spec:** `docs/superpowers/specs/2026-05-18-surface-plot-design.md`

**Baseline:** `fac06d2` on branch `feat/3d-surface-plot`.

---

## File Structure

- Modify `scripts/ferret_plot/kinds/_shared.py`: add `prepare_grid(...)` for shared 2D pivot preparation and optional complete-grid validation.
- Create `scripts/ferret_plot/kinds/surface.py`: render a 3D surface from a prepared grid.
- Modify `scripts/ferret_plot/cli.py`: register `surface` and add its flags.
- Modify `tests/python/conftest.py`: add default argparse attributes for `surface`.
- Modify `tests/python/fixtures.py`: add `tage_capacity_df(...)`, a synthetic two-axis frame with generic branch/pattern amount columns.
- Create `tests/python/test_shared_grid.py`: cover shared pivot shape and complete-grid validation.
- Create `tests/python/test_surface.py`: cover renderer behavior and errors.
- Modify `tests/python/test_cli.py`: add end-to-end PNG coverage for `surface`.
- Modify `docs/cli.md`: document the fourth plot subcommand.

---

## Pre-Flight

- [ ] **Step 0.1: Verify the working tree**

Run:

```bash
git status --short
```

Expected: no tracked source changes. Untracked `.superpowers/` files from brainstorming can be ignored and must not be staged.

- [ ] **Step 0.2: Verify Python tooling**

Run:

```bash
python3 -c "import pandas, matplotlib, numpy; print(pandas.__version__, matplotlib.__version__, numpy.__version__)"
python3 -m pytest --version
```

Expected: both commands exit 0 and print versions.

- [ ] **Step 0.3: Verify current Python tests**

Run:

```bash
./scripts/test_py.sh
```

Expected: all existing Python tests pass before changing plotting code.

---

## Task 1: `refactor(plot): share heatmap grid preparation`

**Goal of commit:** Add a tested grid-preparation helper and keep heatmap output behavior unchanged.

**Files:**
- Modify: `scripts/ferret_plot/kinds/_shared.py`
- Create: `tests/python/test_shared_grid.py`

- [ ] **Step 1.1: Write failing shared-grid tests**

Create `tests/python/test_shared_grid.py`:

```python
"""Tests for shared 2D grid preparation used by heatmap-shaped plots."""

from __future__ import annotations

import pytest
from ferret_plot.errors import PlotError
from ferret_plot.kinds._shared import prepare_grid
from fixtures import dbf_df


class TestPrepareGrid:
    def test_grid_rows_are_y_and_columns_are_x(self):
        branches = (1, 2, 4)
        spacing = (16, 32)
        df = dbf_df(branches=branches, spacing=spacing)

        grid = prepare_grid(df, xcol="branches", ycol="spacing_bytes", value_col="cycles_per_site_min")

        assert grid.shape == (len(spacing), len(branches))
        assert grid.index.tolist() == list(spacing)
        assert grid.columns.tolist() == list(branches)

    def test_require_complete_raises_for_missing_cell(self):
        df = dbf_df(branches=(1, 2), spacing=(16, 32))
        df = df[~((df["branches"] == 2) & (df["spacing_bytes"] == 32))]

        with pytest.raises(PlotError, match="missing grid cells"):
            prepare_grid(
                df,
                xcol="branches",
                ycol="spacing_bytes",
                value_col="cycles_per_site_min",
                require_complete=True,
            )

    def test_default_allows_missing_cell_for_existing_heatmap_behavior(self):
        df = dbf_df(branches=(1, 2), spacing=(16, 32))
        df = df[~((df["branches"] == 2) & (df["spacing_bytes"] == 32))]

        grid = prepare_grid(df, xcol="branches", ycol="spacing_bytes", value_col="cycles_per_site_min")

        assert grid.isna().to_numpy().any()
```

- [ ] **Step 1.2: Run the new tests and verify they fail**

Run:

```bash
./scripts/test_py.sh tests/python/test_shared_grid.py -q
```

Expected: FAIL with an import error because `prepare_grid` does not exist.

- [ ] **Step 1.3: Add `prepare_grid` to `_shared.py`**

Modify `scripts/ferret_plot/kinds/_shared.py` by adding this function below `resolve_heatmap_xy` and above `render_heatmap_cell`:

```python
def prepare_grid(
    sub_df: pd.DataFrame,
    *,
    xcol: str,
    ycol: str,
    value_col: str,
    require_complete: bool = False,
) -> pd.DataFrame:
    """Pivot one subset to a sorted 2D grid.

    `aggfunc="first"` matches the existing heatmap behavior: duplicate
    (X, Y) rows from concatenated CSVs remain deterministic instead of
    being silently averaged.
    """
    grid = (
        sub_df.pivot_table(index=ycol, columns=xcol, values=value_col, aggfunc="first").sort_index().sort_index(axis=1)
    )
    if require_complete and grid.isna().to_numpy().any():
        missing = [
            f"({xcol}={x}, {ycol}={y})"
            for y, row in grid.iterrows()
            for x, value in row.items()
            if pd.isna(value)
        ]
        shown = ", ".join(missing[:5])
        suffix = "" if len(missing) <= 5 else f", ... ({len(missing)} total)"
        raise PlotError(f"missing grid cells for surface plot: {shown}{suffix}")
    return grid
```

In the same file, replace the `pivot = ...` block inside `render_heatmap_cell(...)` with:

```python
    pivot = prepare_grid(sub_df, xcol=xcol, ycol=ycol, value_col=value_col)
```

Keep the rest of `render_heatmap_cell(...)` unchanged.

- [ ] **Step 1.4: Run shared-grid and heatmap tests**

Run:

```bash
./scripts/test_py.sh tests/python/test_shared_grid.py tests/python/test_heatmap.py -q
```

Expected: all selected tests pass.

- [ ] **Step 1.5: Commit shared-grid helper**

Run:

```bash
git add scripts/ferret_plot/kinds/_shared.py tests/python/test_shared_grid.py
git commit -m "refactor(plot): share 2d grid preparation"
```

Expected: commit succeeds. If this environment reports `gpg: signing failed: No secret key`, ask the user before retrying that same commit with `git commit --no-gpg-sign -m "refactor(plot): share 2d grid preparation"`.

---

## Task 2: `feat(plot): add surface renderer`

**Goal of commit:** Add the unit-tested 3D renderer without wiring it into the public CLI yet.

**Files:**
- Modify: `tests/python/conftest.py`
- Modify: `tests/python/fixtures.py`
- Create: `tests/python/test_surface.py`
- Create: `scripts/ferret_plot/kinds/surface.py`

- [ ] **Step 2.1: Add test defaults, synthetic frame, and failing surface tests**

In `tests/python/conftest.py`, update `_KIND_DEFAULTS` to include `surface`:

```python
_KIND_DEFAULTS = {
    "line": {"x": None, "xscale": None, "series": None, "ymax": None},
    "heatmap": {"x": None, "y": None, "logz": False},
    "facets": {"facet": None, "x": None, "y": None, "logz": False},
    "surface": {"x": None, "y": None, "logz": False, "elev": 30.0, "azim": -60.0},
}
```

In `tests/python/fixtures.py`, add this function after `three_axis_df(...)`:

```python
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
```

Create `tests/python/test_surface.py`:

```python
"""Tests for ferret_plot.kinds.surface."""

from __future__ import annotations

import pytest
from conftest import make_args
from ferret_plot.errors import PlotError
from ferret_plot.kinds import surface as surface_kind
from fixtures import dct_df, tage_capacity_df
from matplotlib.colors import LogNorm


def _args(**overrides):
    return make_args("surface", **overrides)


def _surface_axis(fig):
    for ax in fig.axes:
        if getattr(ax, "name", None) == "3d":
            return ax
    raise AssertionError("no 3D axis on figure")


class TestSurfaceMakeFigure:
    def test_default_axes_follow_two_axis_csv_order(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args())
        ax = _surface_axis(fig)
        assert ax.get_xlabel() == "branch_amount"
        assert ax.get_ylabel() == "pattern_amount"
        assert "cycles per site" in ax.get_zlabel()

    def test_explicit_x_y_transpose_base_axes(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args(x="pattern_amount", y="branch_amount"))
        ax = _surface_axis(fig)
        assert ax.get_xlabel() == "pattern_amount"
        assert ax.get_ylabel() == "branch_amount"

    def test_colorbar_present(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args())
        assert len(fig.axes) == 2
        assert _surface_axis(fig) is fig.axes[0]

    def test_logz_sets_surface_lognorm(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args(logz=True))
        ax = _surface_axis(fig)
        assert isinstance(ax.collections[0].norm, LogNorm)

    def test_camera_flags_set_view_angles(self):
        df = tage_capacity_df()
        fig = surface_kind.make_figure(df, _args(elev=42.0, azim=-35.0))
        ax = _surface_axis(fig)
        assert ax.elev == 42.0
        assert ax.azim == -35.0

    def test_one_axis_csv_raises(self):
        df = dct_df(chain_lengths=(100, 200, 300))
        with pytest.raises(PlotError, match="at least 2 varying axis columns"):
            surface_kind.make_figure(df, _args())

    def test_invalid_x_raises(self):
        df = tage_capacity_df()
        with pytest.raises(PlotError, match="not a column"):
            surface_kind.make_figure(df, _args(x="not_a_column"))

    def test_invalid_y_raises(self):
        df = tage_capacity_df()
        with pytest.raises(PlotError, match="not a column"):
            surface_kind.make_figure(df, _args(y="not_a_column"))

    def test_missing_grid_cell_raises(self):
        df = tage_capacity_df(branch_amounts=(64, 128), pattern_amounts=(4, 8))
        df = df[~((df["branch_amount"] == 128) & (df["pattern_amount"] == 8))]
        with pytest.raises(PlotError, match="missing grid cells"):
            surface_kind.make_figure(df, _args())

    def test_logz_rejects_non_positive_values(self):
        df = tage_capacity_df()
        df.loc[df.index[0], "cycles_per_site_min"] = 0.0
        with pytest.raises(PlotError, match="--logz requires positive"):
            surface_kind.make_figure(df, _args(logz=True))
```

- [ ] **Step 2.2: Run the surface tests and verify they fail**

Run:

```bash
./scripts/test_py.sh tests/python/test_surface.py -q
```

Expected: FAIL with an import error because `scripts/ferret_plot/kinds/surface.py` does not exist.

- [ ] **Step 2.3: Create the surface renderer**

Create `scripts/ferret_plot/kinds/surface.py`:

```python
"""3D surface renderer for CSVs with at least 2 varying axis columns."""

from __future__ import annotations

import argparse

import numpy as np
import pandas as pd
from matplotlib import pyplot as plt
from matplotlib.colors import LogNorm, Normalize
from matplotlib.figure import Figure
from mpl_toolkits.mplot3d import Axes3D  # noqa: F401

from ferret_plot.columns import bench_name, resolve_metric
from ferret_plot.errors import PlotError
from ferret_plot.formatting import decimate_indices, human_readable
from ferret_plot.kinds._shared import prepare_grid, resolve_heatmap_xy
from ferret_plot.registry import resolve_defaults


def _z_values(grid: pd.DataFrame) -> np.ndarray:
    try:
        return grid.to_numpy(dtype=float)
    except (TypeError, ValueError) as e:
        raise PlotError("surface plot metric values must be numeric") from e


def _norm(z: np.ndarray, *, logz: bool) -> Normalize:
    zmin = float(np.nanmin(z))
    zmax = float(np.nanmax(z))
    if logz:
        if zmin <= 0:
            raise PlotError("--logz requires positive metric values")
        return LogNorm(vmin=zmin, vmax=zmax)
    return Normalize(vmin=zmin, vmax=zmax)


def _set_position_ticks(axis, labels: list[object]) -> None:
    kept = decimate_indices(labels)
    axis.set_ticks(kept)
    axis.set_ticklabels([human_readable(labels[i]) for i in kept])


def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> Figure:
    metric = resolve_metric(df, metric=args.metric, stat=args.stat)
    defaults = resolve_defaults(df, override=args.benchmark)
    xcol, ycol = resolve_heatmap_xy(df, args, defaults)
    grid = prepare_grid(df, xcol=xcol, ycol=ycol, value_col=metric.column, require_complete=True)
    z = _z_values(grid)
    norm = _norm(z, logz=args.logz)

    x_positions = np.arange(len(grid.columns))
    y_positions = np.arange(len(grid.index))
    x_grid, y_grid = np.meshgrid(x_positions, y_positions)

    fig = plt.figure(figsize=(8, 6))
    ax = fig.add_subplot(111, projection="3d")
    surface = ax.plot_surface(
        x_grid,
        y_grid,
        z,
        cmap="viridis",
        norm=norm,
        linewidth=0,
        antialiased=True,
    )

    _set_position_ticks(ax.xaxis, list(grid.columns))
    _set_position_ticks(ax.yaxis, list(grid.index))
    ax.set_xlabel(xcol)
    ax.set_ylabel(ycol)
    ax.set_zlabel(metric.label)
    ax.set_title(f"{bench_name(df)}: {metric.label} surface ({ycol} × {xcol})")
    ax.view_init(elev=args.elev, azim=args.azim)
    fig.colorbar(surface, ax=ax, shrink=0.75, pad=0.12).set_label(metric.label)
    return fig
```

- [ ] **Step 2.4: Run surface unit tests**

Run:

```bash
./scripts/test_py.sh tests/python/test_surface.py -q
```

Expected: all surface tests pass.

- [ ] **Step 2.5: Run related Python tests**

Run:

```bash
./scripts/test_py.sh tests/python/test_shared_grid.py tests/python/test_heatmap.py tests/python/test_surface.py -q
```

Expected: all selected tests pass.

- [ ] **Step 2.6: Commit surface renderer**

Run:

```bash
git add scripts/ferret_plot/kinds/surface.py tests/python/conftest.py tests/python/fixtures.py tests/python/test_surface.py
git commit -m "feat(plot): add surface renderer"
```

Expected: commit succeeds. If this environment reports `gpg: signing failed: No secret key`, ask the user before retrying that same commit with `git commit --no-gpg-sign -m "feat(plot): add surface renderer"`.

---

## Task 3: `feat(plot): expose surface subcommand`

**Goal of commit:** Register the public CLI, verify it writes a PNG, and document the new command.

**Files:**
- Modify: `scripts/ferret_plot/cli.py`
- Modify: `tests/python/test_cli.py`
- Modify: `docs/cli.md`

- [ ] **Step 3.1: Add failing CLI tests**

Modify the imports in `tests/python/test_cli.py`:

```python
from fixtures import dbf_df, dct_df, tage_capacity_df, three_axis_df
```

Add this test class after `TestHeatmapSubcommand`:

```python
class TestSurfaceSubcommand:
    def test_surface_parser_defaults(self):
        args = cli.build_parser().parse_args(["surface", "input.csv"])
        assert args.kind == "surface"
        assert args.x is None
        assert args.y is None
        assert args.logz is False
        assert args.elev == 30.0
        assert args.azim == -60.0

    def test_surface_produces_png(self, tmp_path):
        csv_path = _write_csv(tage_capacity_df(), tmp_path, "tage.csv")
        out_path = tmp_path / "surface.png"
        rc = cli.main(
            [
                "surface",
                csv_path,
                "--x",
                "branch_amount",
                "--y",
                "pattern_amount",
                "--out",
                str(out_path),
            ]
        )
        assert rc == 0
        assert out_path.exists()
        assert out_path.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"

    def test_surface_invalid_axis_exits_2(self, tmp_path, capsys):
        csv_path = _write_csv(tage_capacity_df(), tmp_path, "tage.csv")
        rc = cli.main(
            [
                "surface",
                csv_path,
                "--x",
                "not_a_column",
                "--y",
                "pattern_amount",
                "--out",
                str(tmp_path / "surface.png"),
            ]
        )
        assert rc == EXIT_USER_ERROR
        assert "not a column" in capsys.readouterr().err
```

- [ ] **Step 3.2: Run the CLI tests and verify they fail**

Run:

```bash
./scripts/test_py.sh tests/python/test_cli.py::TestSurfaceSubcommand -q
```

Expected: FAIL because `surface` is not an accepted subcommand.

- [ ] **Step 3.3: Register `surface` in the CLI**

In `scripts/ferret_plot/cli.py`, update the imports:

```python
from ferret_plot.kinds import facets as facets_kind
from ferret_plot.kinds import heatmap as heatmap_kind
from ferret_plot.kinds import line as line_kind
from ferret_plot.kinds import surface as surface_kind
```

Update the parser description:

```python
        description="Plot a ferret CSV as a line plot, heatmap, facet grid, or 3D surface.",
```

Add this subparser after `heatmap` and before `facets`:

```python
    surface = sub.add_parser("surface", help="3D surface over two varying axes")
    _add_common(surface)
    surface.add_argument("--x", default=None, help="X-axis column")
    surface.add_argument("--y", default=None, help="Y-axis column")
    surface.add_argument("--logz", action="store_true", help="log color scale")
    surface.add_argument("--elev", type=float, default=30.0, help="3D camera elevation angle")
    surface.add_argument("--azim", type=float, default=-60.0, help="3D camera azimuth angle")
    surface.set_defaults(handler=surface_kind.make_figure)
```

- [ ] **Step 3.4: Document `surface` in `docs/cli.md`**

In `docs/cli.md`, change:

```markdown
`scripts/plot.py` exposes three rendering kinds; each accepts a CSV
```

to:

```markdown
`scripts/plot.py` exposes four rendering kinds; each accepts a CSV
```

Add the surface command to the command block:

```text
python3 scripts/plot.py surface FILE.csv [--x=COL] [--y=COL] [--out=PATH] [--metric=auto|cycles|ns] [--stat=min|median] [--logz] [--elev=DEG] [--azim=DEG]
```

Replace the paragraph that starts with "The plot script reads row 0" with:

```markdown
The plot script reads row 0 of the CSV's `benchmark` column to choose
sensible X/Y defaults per benchmark; pass `--x`/`--y` to override (or
`--benchmark=NAME` to force a registry lookup for a stripped CSV).
`line` defaults to a log-base-2 X axis (right for `log2_range` sweeps
like `branches` or `spacing_bytes`); pass `--xscale=linear` for plain
sweeps such as `nested_call_depth`'s `depth=1..64`, which the registry
also picks automatically. Use `surface` for a static 3D view of a
two-axis capacity sweep where the selected metric becomes both height
and color. Use `facets` with `--facet=COL` when a CSV has three or more
varying axes.
```

- [ ] **Step 3.5: Run CLI tests**

Run:

```bash
./scripts/test_py.sh tests/python/test_cli.py -q
```

Expected: all CLI tests pass.

- [ ] **Step 3.6: Commit CLI and docs**

Run:

```bash
git add scripts/ferret_plot/cli.py tests/python/test_cli.py docs/cli.md
git commit -m "feat(plot): expose surface subcommand"
```

Expected: commit succeeds. If this environment reports `gpg: signing failed: No secret key`, ask the user before retrying that same commit with `git commit --no-gpg-sign -m "feat(plot): expose surface subcommand"`.

---

## Task 4: Final Verification

**Goal:** Verify the complete plotting package and produce one real surface PNG from synthetic CSV data.

- [ ] **Step 4.1: Run the full Python test suite**

Run:

```bash
./scripts/test_py.sh
```

Expected: all Python tests pass.

- [ ] **Step 4.2: Run ruff on plotting code and Python tests**

Run:

```bash
ruff format --check scripts/ tests/python/
ruff check scripts/ tests/python/
```

Expected: both commands exit 0.

- [ ] **Step 4.3: Generate a real surface PNG**

Run:

```bash
python3 - <<'PY'
from pathlib import Path
import sys

sys.path.insert(0, "scripts")
sys.path.insert(0, "tests/python")
from fixtures import tage_capacity_df

out = Path("/tmp/ferret-surface-smoke.csv")
tage_capacity_df().to_csv(out, index=False)
print(out)
PY
python3 scripts/plot.py surface /tmp/ferret-surface-smoke.csv --x=branch_amount --y=pattern_amount --out=/tmp/ferret-surface-smoke.png
file /tmp/ferret-surface-smoke.png
```

Expected: the final command prints a PNG image description for `/tmp/ferret-surface-smoke.png`.

- [ ] **Step 4.4: Inspect final git status**

Run:

```bash
git status --short
```

Expected: only intentional committed changes are absent from status. Untracked `.superpowers/` files from brainstorming can remain untracked and must not be staged.

---

## Acceptance Mapping

- `surface FILE.csv --x=COL1 --y=COL2 --out=OUT.png` writes a PNG: Task 3.1 and Task 4.3.
- Surface uses metric as Z height and colormap: Task 2.3 and Task 2.1 `test_logz_sets_surface_lognorm`.
- Existing `line`, `heatmap`, and `facets` behavior remains unchanged: Task 1.4, Task 3.5, Task 4.1.
- Missing grid cells fail clearly: Task 1.1 and Task 2.1.
- No TAGE-specific registry defaults: Task 2 fixture uses `synthetic_tage_capacity`; no `registry.py` changes appear in the file list.

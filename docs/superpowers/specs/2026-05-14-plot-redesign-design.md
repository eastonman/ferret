# `scripts/plot.py` Redesign — Design Spec

**Date:** 2026-05-14
**Status:** Design — pending implementation plan
**Baseline:** `b0f01f6` (`main`)
**Working branch:** `feat/plot-redesign`

## Goal

Replace the single-file `scripts/plot.py` (107 lines, one plot shape, hardcoded
metadata list, fragile auto-axis heuristic) with a small Python package that:

1. Exposes three subcommands — `line`, `heatmap`, `facets` — so today's
   1-axis CSVs (`dependent_chain_throughput`), today's 2-axis CSVs
   (`direct_branch_footprint`), and tomorrow's ≥3-axis sweeps
   (TAGE capacity etc.) each get a natural rendering.
2. Makes `spacing_bytes`-as-X a first-class, documented invocation for
   `direct_branch_footprint`.
3. Centralizes per-benchmark defaults in a tiny registry keyed off the
   `benchmark` CSV column.
4. Ships with pytest coverage so the script's behavior is no longer
   "the maintainer eyeballs the PNG."

The C++ side is unchanged. `scripts/freq.py` is out of scope.

## Motivation

Concrete signals on the current `scripts/plot.py`:

- One file, one plot shape (line+series). 2-axis sweeps render as a
  legend explosion (15 branch values × 4 spacing values = up to 60
  curves with `direct_branch_footprint`).
- No way to render the 2-axis sweep as a heatmap — the natural shape
  for "value-over-2D-grid" data.
- `auto_x_column` picks "first varying non-metadata column"
  (`plot.py:48-54`), which is column-order-dependent. For
  `direct_branch_footprint` it always picks `branches`, with no
  per-benchmark override hook short of `--x=`.
- `METADATA_COLS` (`plot.py:33-45`) is hardcoded. New CSV columns
  added on the C++ side silently become series-grouping keys until
  someone edits the script.
- Y-column selection (`plot.py:67-72`) is "cycles if present, else ns";
  no way to ask for median, no way to force ns when both are present.
- `df["benchmark"].iloc[0]` (`plot.py:96`) silently picks the first
  benchmark name even when a CSV has been (incorrectly) concatenated.
- No tests. The script is not importable as a module — `main()` is a
  ~50-line argparse-and-plot block.

The TAGE capacity benchmark slated for a future PR will sweep ≥2
parameters (predictor budget × history depth at minimum, possibly with
a variant axis). It's the second benchmark family where heatmaps and
facets are the right rendering, so doing this restructure now is no
longer speculative.

## Non-goals

These are explicitly out of scope to keep the diff focused:

- **No changes to ferret CSV format.** Per-benchmark hints live in a
  Python-side registry; no new comment headers, sidecar files, or
  metadata columns.
- **No interactive backends.** matplotlib `Agg` only, output is PNG
  files or an on-screen figure as today.
- **No new dependencies beyond pandas + matplotlib + pytest.**
  Specifically: no `seaborn`, no `plotly`.
- **No error bars or median-as-shaded-band.** `--stat=median` switches
  the rendered value but does not overlay a second statistic.
- **No multi-CSV concatenation.** One CSV per invocation.
- **No image-diff regression tests.** Structural assertions on the
  matplotlib `Axes` only.
- **No changes to `scripts/freq.py`.**
- **No changes to the C++ benchmark registry.**

## Target file layout

```
scripts/
  plot.py                      [shrunk] thin entry shim, ~10 lines
  ferret_plot/                 [new]
    __init__.py
    cli.py                     argparse subparsers, dispatch
    columns.py                 metadata set, metric/stat resolution, axis classification
    registry.py                BenchmarkDefaults dataclass, DEFAULTS dict, lookup
    formatting.py              human_readable + tick helpers
    errors.py                  one small exception type for clean CLI exits
    kinds/
      __init__.py
      line.py                  line+series renderer
      heatmap.py               single-panel heatmap renderer
      facets.py                grid-of-heatmaps renderer
tests/
  python/                      [new]
    conftest.py                prepends scripts/ to sys.path
    fixtures.py                synthetic-DataFrame builders per benchmark
    test_columns.py
    test_registry.py
    test_line.py
    test_heatmap.py
    test_facets.py
    test_cli.py                end-to-end: argv → file written, exit code
```

## Module APIs

### `scripts/plot.py`

```python
#!/usr/bin/env python3
"""Thin entry shim. See scripts/ferret_plot/cli.py for the real CLI."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ferret_plot.cli import main

if __name__ == "__main__":
    sys.exit(main())
```

Kept under `scripts/` for README muscle-memory parity. The `sys.path`
prepend is the project's "scripts are scripts, not an installed
package" tradeoff — same shape as how `format.sh` and `lint.sh` are
just shell wrappers, not packaged tools.

### `scripts/ferret_plot/columns.py`

```python
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
    column: str           # e.g. "cycles_per_site_min"
    label: str            # e.g. "cycles per site (min)"
    short: str            # e.g. "cycles"

def resolve_metric(df: pd.DataFrame, metric: str, stat: str) -> Metric:
    """metric in {'auto', 'cycles', 'ns'}, stat in {'min', 'median'}.
    Raises PlotError if requested column is missing or all-NaN.
    'auto' prefers cycles when cycles_per_site_<stat> has >=1 non-NaN row."""

def axis_columns(df: pd.DataFrame) -> list[str]:
    """Non-metadata columns. Order = CSV column order."""

def varying_axis_columns(df: pd.DataFrame) -> list[str]:
    """axis_columns filtered to those with nunique() > 1."""
```

The metric resolver replaces today's inline `if "cycles_per_site_min"
in df.columns and df["cycles_per_site_min"].notna().any()` block.
`PlotError` is the one exception type the CLI converts to a clean
`sys.exit(2)` with a message; everything else bubbles up as a real
traceback for actual bugs.

### `scripts/ferret_plot/registry.py`

```python
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
    """Returns benchmark name to look up in DEFAULTS, or None.

    1. override (from --benchmark=NAME) wins.
    2. else df['benchmark'].iloc[0] if column exists and df is non-empty.
    3. raises PlotError if df['benchmark'].nunique() > 1
       (mixed-benchmark CSV).
    4. returns None if the column is absent — caller falls back
       to ordering rules and logs an info message."""

def resolve_defaults(df: pd.DataFrame, override: str | None) -> BenchmarkDefaults:
    name = detect_benchmark(df, override)
    return DEFAULTS.get(name, BenchmarkDefaults()) if name else BenchmarkDefaults()
```

Mixed-benchmark CSV → `PlotError("mixed-benchmark CSV: found ['a', 'b']; pre-filter or pass --benchmark=NAME")`.
Missing-from-registry → returns empty defaults, callers fall through
to ordering rules silently (it's not an error, just "no hint").
Missing `benchmark` column → logs one info-level message via `print`
to stderr (no logging dependency).

### `scripts/ferret_plot/formatting.py`

```python
def human_readable(x: float, _pos: int | None = None) -> str:
    """G/M/K suffixes for >= 2^10. Identical to today's behavior."""

def apply_log2_axis(axis, values: Iterable[float]) -> None:
    """Sets log scale base 2, ticks at unique sorted values,
    human_readable major formatter, NullFormatter minor."""
```

Lifted directly from today's `plot.py:23-30` and `plot.py:88-92`,
named, and shared across all three kinds.

### `scripts/ferret_plot/kinds/line.py`

```python
def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> matplotlib.figure.Figure:
    """Line plot. X = args.x or registry.line_x or first-varying-axis.
    Series = all remaining varying axis columns (filtered by --series=
    if provided). Y = resolve_metric(df, args.metric, args.stat).column.
    --ymax caps Y. Same visual output as today's plot.py for 1-axis CSVs."""
```

Functional parity with today's renderer for 1-axis CSVs. For ≥2-axis
CSVs (the `direct_branch_footprint` case) the line renderer remains
the way to "read exact values" — the heatmap is the new option,
not the replacement.

### `scripts/ferret_plot/kinds/heatmap.py`

```python
def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> matplotlib.figure.Figure:
    """2D heatmap.
    X = args.x or registry.heatmap_x or first-varying-axis.
    Y = args.y or registry.heatmap_y or second-varying-axis.
    Z = resolve_metric(...).column.
    Raises PlotError if the CSV has fewer than 2 varying axis columns.
    Pivot via df.pivot_table(index=Y, columns=X, values=Z, aggfunc='first').
    imshow with LogNorm when --logz, else linear Normalize.
    NaN cells (e.g., jit_failed rows) are masked and rendered with
    a hatched overlay so absence is visible.
    Tick labels = unique X/Y values formatted with human_readable.
    Colorbar labeled with metric.label."""
```

`pivot_table(aggfunc='first')` rather than the default `mean` because
each (X, Y) cell already corresponds to one ferret row; if duplicates
exist (concatenated CSVs) we want a hard signal, not a silent average.
A follow-up could detect the duplicate case and surface it as
`PlotError`; the initial implementation just takes the first match
to keep the spec minimal.

### `scripts/ferret_plot/kinds/facets.py`

```python
def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> matplotlib.figure.Figure:
    """Grid of heatmaps, one per unique value of args.facet.
    --facet=COL is required; the registry.facet_col default applies if omitted.
    PlotError if --facet is unset and the registry has no default.
    Remaining two varying axes become each subplot's X/Y
    (resolved via args.x / args.y / registry, same as heatmap.py).
    Grid layout: ncols = ceil(sqrt(N)), nrows packed.
    Shared color scale across all subplots; one shared colorbar on the right.
    Subplot title = '<facet>=<value>'."""
```

The "shared color scale" decision matters for visual interpretability:
each subplot's `imshow` is given the same `vmin`/`vmax` (computed from
the full Z column across all facet values), so cell colors are
comparable across subplots, not normalized per-panel.

### `scripts/ferret_plot/cli.py`

```python
def build_parser() -> argparse.ArgumentParser:
    ap = argparse.ArgumentParser(prog="plot.py")
    sub = ap.add_subparsers(dest="kind", required=True)

    def common(sp):
        sp.add_argument("csv")
        sp.add_argument("--out", default=None)
        sp.add_argument("--benchmark", default=None,
                        help="override registry lookup (rare)")
        sp.add_argument("--metric", default="auto",
                        choices=["auto", "cycles", "ns"])
        sp.add_argument("--stat", default="min",
                        choices=["min", "median"])

    line = sub.add_parser("line")
    common(line)
    line.add_argument("--x", default=None)
    line.add_argument("--series", default=None,
                      help="comma-separated; if set, pin series cols")
    line.add_argument("--ymax", type=float, default=None)

    heat = sub.add_parser("heatmap")
    common(heat)
    heat.add_argument("--x", default=None)
    heat.add_argument("--y", default=None)
    heat.add_argument("--logz", action="store_true")

    fac = sub.add_parser("facets")
    common(fac)
    fac.add_argument("--facet", default=None)
    fac.add_argument("--x", default=None)
    fac.add_argument("--y", default=None)
    fac.add_argument("--logz", action="store_true")

    return ap

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

_DISPATCH = {
    "line":    line_kind.make_figure,
    "heatmap": heatmap_kind.make_figure,
    "facets":  facets_kind.make_figure,
}
```

`main()` takes `argv=None` so tests can call it with synthetic argv
without subprocess overhead. `_emit` writes to `args.out` if set,
otherwise calls `plt.show()` (parity with today).

## Testing strategy

`tests/python/conftest.py` prepends `scripts/` to `sys.path` so the
package imports cleanly without an editable install. `pytest tests/python`
is the entry point; it's wired into a new `scripts/test_py.sh` line in
`scripts/lint.sh`'s sibling slot (the existing `lint.sh` only runs ruff
today, so we add a small `test_py.sh` rather than overloading lint).

`fixtures.py` provides one synthetic DataFrame builder per registered
benchmark:

```python
def dbf_df(*, branches=(1, 2, 4, 8), spacing=(16, 32, 64), with_freq=True) -> pd.DataFrame: ...
def dct_df(*, chain_lengths=(100_000_000,), with_freq=True) -> pd.DataFrame: ...
```

These mirror real ferret output rows (same column names, same dtypes).

**`test_columns.py`** — `METADATA_COLS` membership stability;
`resolve_metric` happy paths (`auto` picks cycles when present, falls
back to ns; explicit `cycles` raises `PlotError` when the cycles
columns are absent; `stat='median'` resolves to the `_median` column);
`axis_columns` and `varying_axis_columns` for the synthetic frames.

**`test_registry.py`** — `detect_benchmark` precedence (override wins
over CSV; non-empty CSV uses row-0; mixed-benchmark raises;
missing-column returns None); for every key in `DEFAULTS`, instantiate
the corresponding `*_df()` fixture and assert that every non-None
column reference in the entry exists in that fixture (keeps the
registry honest against fixture drift).

**`test_line.py`** — given `dbf_df()` with `--x=spacing_bytes`,
`make_figure` returns a Figure whose single `Axes` has
`len(get_lines()) == len(unique_branches)`, X tick locations match the
unique spacing values, and the X-axis label reads `spacing_bytes`.
Same Figure with `--x` omitted picks `branches` via registry.
`dct_df()` (1 axis) produces a single line.

**`test_heatmap.py`** — `dbf_df()` with no flags produces an `imshow`
artist with shape `(len(spacing), len(branches))`. `--x=spacing_bytes`
transposes it. `--logz` causes the artist's norm to be `LogNorm`.
A 1-axis CSV raises `PlotError`.

**`test_facets.py`** — synthetic 3-axis frame (`branches`, `spacing`,
`variant`); `--facet=variant` produces N subplots where N is the
number of variant values; all subplots share `vmin`/`vmax`; missing
`--facet` and no registry default raises `PlotError`.

**`test_cli.py`** — invokes `cli.main(["line", csv_path, "--out", out_path])`
with a tmp CSV file; asserts `out_path` exists and is a non-empty
PNG. One test per kind. One test that mixed-benchmark CSV produces
exit code 2 and a message on stderr.

Tests use `matplotlib.use("Agg")` set at conftest level. No
image-diff comparisons; structural assertions only.

## Doc changes

### `README.md`

- Step 3 of the two-step cycle workflow becomes
  `python3 scripts/plot.py line /tmp/btb.csv --out=/tmp/btb.png`.
- A new short example block follows it showing the
  `spacing_bytes`-as-X invocation and the heatmap:

  ```sh
  # Line plot with spacing_bytes on X (branches becomes the legend).
  python3 scripts/plot.py line /tmp/btb.csv --x=spacing_bytes --out=/tmp/btb-by-spacing.png

  # 2D heatmap (branches × spacing_bytes), cycles per site as color.
  python3 scripts/plot.py heatmap /tmp/btb.csv --out=/tmp/btb-heatmap.png
  ```

- A new "Plot subcommands" subsection near the "Per-benchmark options"
  block documents the three kinds, the registry concept ("ferret embeds
  the benchmark name in every CSV row; the plot script reads row 0 to
  choose sensible X/Y defaults; pass `--x`/`--y` to override"), and
  the TAGE-style pattern ("use `facets` with `--facet=COL` when you
  have ≥3 varying axes").

### Spec / plan

- This spec at `docs/superpowers/specs/2026-05-14-plot-redesign-design.md`.
- Implementation plan at `docs/superpowers/plans/2026-05-14-plot-redesign.md`
  (written by the writing-plans skill in the next step).

## Migration order

Commits land in this order. Each is independently buildable and the
test suite (C++ `ctest` + new Python `pytest tests/python`) passes
on each.

1. **`refactor(plot): extract metadata + metric resolution into ferret_plot.columns`**
   New `scripts/ferret_plot/__init__.py`, `columns.py`, `errors.py`,
   `formatting.py`. `scripts/plot.py` continues to work unchanged —
   it imports the new helpers but keeps the single-command CLI shape.
   New `tests/python/{conftest.py,fixtures.py,test_columns.py}` plus
   the `scripts/test_py.sh` wrapper.

2. **`refactor(plot): introduce ferret_plot.registry with per-benchmark defaults`**
   New `registry.py`. `scripts/plot.py` consults the registry for the
   default X column instead of `auto_x_column`. Behavior unchanged on
   `dependent_chain_throughput` (single axis); on
   `direct_branch_footprint` the default X is now explicitly
   `branches` via the registry rather than implicitly via CSV column
   order. New `tests/python/test_registry.py`.

3. **`feat(plot): subcommand CLI with line kind`**
   New `cli.py` and `kinds/__init__.py`, `kinds/line.py`. `scripts/plot.py`
   becomes the entry shim. Invocation changes from
   `plot.py FILE.csv` to `plot.py line FILE.csv`. README updated.
   New `tests/python/{test_line.py,test_cli.py}`.

4. **`feat(plot): heatmap kind for 2-axis benchmarks`**
   New `kinds/heatmap.py`. `--x`/`--y`/`--logz` flags wired. New
   `tests/python/test_heatmap.py`. README example block added.

5. **`feat(plot): facets kind for >=3-axis sweeps`**
   New `kinds/facets.py`. `--facet=COL` flag wired with registry
   fallback. New `tests/python/test_facets.py`. README "Plot
   subcommands" subsection added.

The break between commits 2 and 3 is the user-visible CLI change
(positional subcommand required). README, repo shell history, and any
external snippets need to be updated at commit 3. Commits 4 and 5 are
additive.

## Follow-ups (not in this redesign)

- **`--stat=both`** rendering median as a shaded band around the min
  line. Useful, but doubles the kind-specific rendering surface.
- **Duplicate-row detection** in heatmap pivots, surfaced as
  `PlotError` rather than silently keeping the first match.
- **CSV-side hint comments** (e.g., `# x_axis=branches` header
  comment emitted by `CsvWriter`). Avoids the Python registry
  drifting from C++ benchmark definitions, at the cost of a small
  C++ change and an output-format tweak. Defer until a third
  benchmark family arrives.
- **Image-diff regression tests** (e.g., `pytest-mpl`). Defer until
  visual regressions actually bite.

## Acceptance criteria

- `scripts/plot.py` is ≤ 15 lines (entry shim only).
- `scripts/ferret_plot/` package present with the file list above; no
  single file in the package exceeds ~150 lines.
- `python3 scripts/plot.py line  /tmp/btb.csv --out=A.png` produces a
  line plot with `branches` on X and `spacing_bytes` as series (default
  case).
- `python3 scripts/plot.py line  /tmp/btb.csv --x=spacing_bytes
  --out=B.png` produces a line plot with `spacing_bytes` on X and
  `branches` as series.
- `python3 scripts/plot.py heatmap /tmp/btb.csv --out=C.png` produces
  a 2D heatmap with branches × spacing_bytes and a colorbar.
- `python3 scripts/plot.py facets /tmp/some3axis.csv --facet=variant
  --out=D.png` produces a grid of heatmaps sharing a single colorbar.
- `pytest tests/python` is green; coverage includes every public
  function in `columns.py`, `registry.py`, and each kind's
  `make_figure`.
- `ruff format --check scripts/ tests/python/` and
  `ruff check scripts/ tests/python/` are clean.
- README's two-step cycle workflow renders correctly with the new
  CLI, and the new "Plot subcommands" subsection is in place.
- `scripts/freq.py` is byte-identical to baseline.
- `src/`, `include/`, `benchmarks/`, and `tests/` (C++ tree) are
  byte-identical to baseline.

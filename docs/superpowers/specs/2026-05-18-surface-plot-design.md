# Surface Plot Command Design

## Goal

Add a static 3D surface plot to the existing `scripts/plot.py` plotting
package so upcoming TAGE capacity experiments can inspect the interaction
between pattern amount and branch amount. The TAGE benchmark column names
are not final, so the first version must stay generic and work from explicit
axis selection.

## User-Facing CLI

Add a fourth plot kind:

```bash
python3 scripts/plot.py surface FILE.csv --x=branch_placeholder --y=pattern_placeholder --out=tage-surface.png
```

`surface` uses the same common arguments as the existing plot kinds:

- `FILE.csv`
- `--out=PATH`
- `--benchmark=NAME`
- `--metric=auto|cycles|ns`
- `--stat=min|median`

It also accepts:

- `--x=COL` and `--y=COL` for the two base-plane axes.
- `--logz` for logarithmic color normalization.
- `--elev=FLOAT` and `--azim=FLOAT` for the static 3D camera angle.

Until the TAGE benchmark lands with stable column names, the documented
TAGE usage should pass `--x` and `--y` explicitly. If either axis is omitted,
the command will fall back to the same generic axis resolution rules used by
`heatmap`: registry hint first when available, otherwise the first two
varying non-metadata columns.

## Rendering Behavior

`surface` pivots the selected X/Y columns into a 2D grid and uses the selected
metric column as Z. The surface height is Z, and face colors are mapped from
the same Z values through matplotlib's `viridis` colormap. The figure includes
a colorbar labeled with the resolved metric, such as `cycles per site (min)`.

The renderer stays static PNG through the existing matplotlib `Agg` pipeline.
It does not add interactive rotation, Plotly, seaborn, or other dependencies.

Defaults:

- Colormap: `viridis`, matching current heatmaps.
- Metric/stat: existing `--metric=auto|cycles|ns` and `--stat=min|median`.
- Output: existing `fig.savefig(..., dpi=400, bbox_inches="tight")` path.
- Title: benchmark name, metric label, and X/Y axis names.
- Axis labels: X, Y, and metric label on Z.

Missing grid cells must not be silently interpolated. The implementation may
raise a clear `PlotError` that identifies the missing grid problem.

## Code Structure

Add `scripts/ferret_plot/kinds/surface.py` with the same public shape as the
existing kind modules:

```python
def make_figure(df: pd.DataFrame, args: argparse.Namespace) -> Figure:
    ...
```

Register the new kind in `scripts/ferret_plot/cli.py` by importing the module,
adding the `surface` subparser, attaching the common arguments, and setting
`handler=surface_kind.make_figure`.

Reuse existing column and metric helpers:

- `resolve_metric`
- `bench_name`
- `resolve_defaults`
- `resolve_heatmap_xy`

Extract shared grid preparation from `scripts/ferret_plot/kinds/_shared.py`
if useful. The important boundary is that heatmap and surface must resolve
their X/Y grid consistently.

Do not add TAGE-specific registry defaults in this change. The TAGE benchmark
will decide real column names later.

## Error Handling

Use existing `PlotError` behavior and messages aligned with heatmap/facets:

- Fewer than two varying axes: fail clearly.
- Invalid `--x` or `--y`: fail clearly.
- `--x` equal to `--y`: fail clearly.
- Non-positive Z values with `--logz`: fail clearly if matplotlib cannot
  normalize them safely.
- Duplicate `(x, y)` rows: keep the current heatmap-compatible
  `aggfunc="first"` behavior for v1.

## Tests

Add focused pytest coverage under `tests/python/`:

- CLI parser accepts the `surface` subcommand.
- Default X/Y choice follows the same rules as heatmap for a two-axis frame.
- Explicit `--x`/`--y` transposes the surface grid as expected.
- The generated figure contains a 3D axis and a colorbar axis.
- `--logz` applies logarithmic color normalization.
- One-axis CSVs raise `PlotError`.
- Invalid axis columns raise `PlotError`.

Synthetic tests can use placeholder TAGE-like columns such as
`branch_placeholder` and `pattern_placeholder`; these names are not product
API and should not be added to the runtime registry.

## Out Of Scope

- Interactive plotting or browser output.
- Plotly or other new visualization dependencies.
- Contour projection overlays.
- TAGE benchmark implementation.
- TAGE-specific plot defaults before final CSV column names exist.

## Acceptance Criteria

- `python3 scripts/plot.py surface FILE.csv --x=COL1 --y=COL2 --out=OUT.png`
  writes a PNG surface plot for a valid two-axis CSV.
- The surface uses Z height and colormap from the selected metric column.
- Existing `line`, `heatmap`, and `facets` behavior remains unchanged.
- The Python test suite covers the new kind and passes.

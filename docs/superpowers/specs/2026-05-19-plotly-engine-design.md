# Plotly Plot Engine Migration Design

## Goal

Replace matplotlib with plotly + kaleido as the plot engine behind
`scripts/plot.py` for all four kinds (`line`, `heatmap`, `facets`,
`surface`). Motivations, in priority order:

1. Per-pixel-smooth terrain coloring on the 3D surface plot.
   Matplotlib's `plot_surface` paints each quad with a single face color
   (flat shading); plotly's `go.Surface` renders via WebGL with
   per-vertex Gouraud color interpolation, which produces a smooth
   colormap gradient across every face on a regular grid.
2. Interactive HTML output for all kinds, including hover tooltips on
   2D line and heatmap plots so exact values at any point are readable
   without re-running the benchmark.
3. Better 3D camera UX (interactive rotate/zoom) so finding a readable
   angle for capacity-cliff surfaces is no longer a guess-and-rerun loop.

End state: a single-engine plot package with matplotlib removed from
`requirements.txt` and the Nix dev shell.

## Scope

All four plot kinds migrate. Implementation order is `surface` first
(visual motivator; proves the shared scaffolding), then `heatmap`,
`facets`, `line` in any order. CLI surface stays identical to today
with two additions (`--format`, `--html-js`) and one per-kind addition
(`--cmap` on `heatmap`/`facets`/`surface`).

## User-Facing CLI

All existing subcommands, positional args, and per-kind flags are
preserved. The four kinds keep their current help text and behavior
shape. New common flags (applied to every kind via `_add_common`):

- `--format=html|png|svg|pdf|jpg|webp` — output format override.
- `--html-js=cdn|inline|sibling` — controls how plotly.js is bundled in
  HTML output. Default `cdn`.

New per-kind flags:

- `heatmap`, `facets`: `--cmap=NAME`, default `Viridis`.
- `surface`: `--cmap=NAME`, default `Earth`.

Output format resolution:

1. `--format` explicit → use it.
2. Otherwise, `--out` ends with `.html|.png|.svg|.pdf|.jpg|.webp` → infer
   from extension.
3. Otherwise, `--out` omitted → write to a temp `.html` and
   `webbrowser.open` it.
4. Otherwise (e.g. `--out=foo` with no extension and no `--format`) →
   `PlotError` asking for one or the other.

Example usage after migration:

```sh
# Static PNG, same UX as today.
python3 scripts/plot.py surface /tmp/btb.csv --out=/tmp/surface.png

# Interactive HTML, default CDN-loaded plotly.js (~50 KB file).
python3 scripts/plot.py surface /tmp/btb.csv --out=/tmp/surface.html

# Self-contained HTML for offline sharing (~5 MB file).
python3 scripts/plot.py heatmap /tmp/btb.csv --out=/tmp/h.html --html-js=inline

# No --out: open an interactive HTML in the system browser.
python3 scripts/plot.py line /tmp/btb.csv
```

## Architecture

Four layers, each with one job.

### Layer 1 — `kinds/*.py` (figure builders)

Each kind exports `make_figure(df, args) -> plotly.graph_objects.Figure`.
Identical signature to today; only the return type changes. All
matplotlib imports leave these modules.

### Layer 2 — `ferret_plot/output.py` (new)

```python
def emit(fig, *, out: str | None, fmt: str | None, html_js: str) -> None
```

Resolves `(out, fmt)` to a concrete format per the rules above. For
HTML, calls `fig.write_html(out, include_plotlyjs=<resolved>, full_html=True)`
where `html_js` maps `{"cdn": "cdn", "inline": True, "sibling": "directory"}`.
For image formats, calls `fig.write_image(out, format=fmt, width=W, height=H, scale=2)`
with `(W, H) = (1600, 1000)` for non-surface kinds and `(2000, 1600)`
for surface (mirrors today's 8×5 and 10×8 matplotlib figsizes at
roughly `dpi=400`). For the no-`--out` case, writes a temp HTML to
`tempfile.NamedTemporaryFile(suffix=".html", delete=False)` and calls
`webbrowser.open(...)`.

On the first image-format export per process, probes for Chrome by
checking `shutil.which` against the kaleido browser-name list
(`chromium`, `chromium-browser`, `chrome`, `Chrome`, `google-chrome`,
`google-chrome-stable`). On miss raises a `PlotError` with the install
hint:

```
kaleido needs Chrome or Chromium installed.
In the Nix devshell this is provided automatically.
Otherwise run `python -m plotly.io._kaleido install_chrome`
or install via your package manager.
```

Probe result is cached on the module so repeated exports skip the
re-check.

### Layer 3 — `ferret_plot/formatting.py` (kept, engine-neutral)

`decimate_indices` and `human_readable` stay unchanged — they don't
import matplotlib today and don't need to import plotly tomorrow.
Callers feed the resulting `tickvals` + `ticktext` into plotly's
axis configuration directly. `apply_axis` (matplotlib-specific) is
removed; its callers build plotly axis dicts inline since the logic
is short.

### Layer 4 — `ferret_plot/cli.py` (dispatch)

Builds the argparse tree (gains `--format`, `--html-js`, `--cmap`),
reads the CSV, calls `args.handler(df, args)`, hands the result to
`output.emit(fig, out=args.out, fmt=args.format, html_js=args.html_js)`.
No matplotlib import after the migration.

### Shared 3D / heatmap helper (`kinds/_shared.py`)

`resolve_heatmap_xy` and `prepare_grid` stay (engine-neutral pandas).
`render_heatmap_cell` (matplotlib-specific) is replaced with:

```python
def build_heatmap_trace(
    grid: pd.DataFrame,
    *,
    value_label: str,
    logz: bool,
    cmap: str,
    cmin: float | None = None,
    cmax: float | None = None,
    coloraxis: str | None = None,
) -> plotly.graph_objects.Heatmap
```

Returns a configured heatmap trace. Used directly by `heatmap` (one
trace, attaches its own colorbar) and by `facets` (per-subplot traces,
all sharing a single `coloraxis` so one colorbar covers the figure).

## Per-Kind Rendering

### `line` → `go.Scatter` / `go.Scattergl`

- One trace per series-key tuple, same grouping logic as today
  (`df.groupby(series_cols)`). No series columns → one trace.
- Switch to `go.Scattergl` when total points across all traces > 5000
  to keep large multi-series plots snappy.
- `mode="lines+markers"`, `marker.size=4`, `line.width=1.5` — visually
  comparable to today's `markersize=2, linewidth=1.0` tuned for
  plotly's renderer.
- X axis: when `xscale="log"`, set `type="log"` and pass the original
  (un-transformed) x values; plotly maps them itself. Spacing is
  controlled by explicit `tickvals` (the chosen subset of original
  values) plus `ticktext` (formatted via `human_readable`) — same
  decimation we use today, just fed into plotly instead of matplotlib.
  When `xscale="linear"`, set `type="linear"` and the same
  `tickvals`/`ticktext` apply.
- Y axis: `rangemode="tozero"`, `range=[0, args.ymax]` when `--ymax`
  is given.
- Hover: default plotly template (`x`, `y`, trace name) — already
  shows series label + exact value.

### `heatmap` → `go.Heatmap`

- `z = grid.values`, `x = list(grid.columns)`, `y = list(grid.index)`.
- `colorscale=args.cmap` (default `Viridis`), colorbar title is
  `metric.label`.
- `--logz`: pre-transform `z` with `np.log10` before passing to the
  trace; colorbar `tickvals` are `log10` of round-number values,
  `ticktext` are the originals formatted via `human_readable`. (Plotly's
  `colorscale` does not accept a log norm directly; pre-transforming
  is the idiomatic approach and keeps the colorbar readable in original
  units.)
- NaN cells: plotly renders NaN as transparent by default. To match
  today's grey "missing" indication for the heatmap kind, set a
  layout background or overlay a second `go.Heatmap` trace with a
  uniform grey color at NaN positions. Pick whichever produces the
  cleaner visual; document the choice in code.
- Tick decimation: explicit `xaxis.tickvals`/`xaxis.ticktext` and same
  for `yaxis` from `decimate_indices` + `human_readable`.
- Title: `f"{bench_name(df)}: {metric.label} ({ycol} × {xcol})"`.

### `facets` → subplot grid of `go.Heatmap`

- `plotly.subplots.make_subplots(rows=nrows, cols=ncols,
  subplot_titles=[f"{facet}={v}" for v in facet_values])`.
- One heatmap trace per facet value, all set to
  `coloraxis="coloraxis"` (single shared colorbar). Layout
  `coloraxis.colorscale=args.cmap`, `coloraxis.cmin`/`cmax` from the
  global metric range (mirrors today's `Normalize(vmin, vmax)` across
  subplots), `coloraxis.colorbar.title=metric.label`.
- `--logz`: pre-log `z` per trace; `coloraxis.cmin`/`cmax` use the
  logged range; colorbar ticks reverse-transform to display original
  values.
- Subplot ordering and grid sizing match today
  (`ncols = ceil(sqrt(n))`, `nrows = ceil(n / ncols)`).
- Figure title: `f"{bench_name(df)}: {metric.label}"` via
  `layout.title`.
- Per-subplot axis ticks: explicit `tickvals`/`ticktext` from the same
  helpers, applied per-subplot via `make_subplots`-returned axis refs.

### `surface` → `go.Surface`

- `z = grid.values`, `x = np.arange(len(grid.columns))`,
  `y = np.arange(len(grid.index))` — same index-based base plane as
  today since CSV X/Y are categorical sweep values.
- `colorscale=args.cmap` (default `Earth`), `cmin`/`cmax` from finite
  `z`.
- `--logz`: pre-log `z` for color mapping (`surfacecolor=np.log10(z)`,
  keep `z` at original scale so Z-axis height stays in original
  units); colorbar ticks reverse-transform.
- 3D scene axes get explicit `tickvals`/`ticktext` from
  `decimate_indices` + `human_readable`. X/Y axis titles are CSV
  column names; Z axis title is `metric.label`.
- Camera: convert `--elev`/`--azim` (degrees) to a Cartesian
  `scene.camera.eye` position. Defaults `elev=20`, `azim=-13` give the
  same default angle as today.
- `scene.aspectratio` mirrors today's `set_box_aspect` formula
  (column count × 1.25, row count × 1.5, max × 0.95, normalized).
- `lighting=dict(ambient=0.6, diffuse=0.8, specular=0.05,
  roughness=0.5, fresnel=0.2)` — close to plotly defaults with a
  slightly higher ambient for readability on power-of-2 sweeps.
- Colorbar title: `metric.label`.
- Figure title: `f"{bench_name(df)}: {metric.label} surface ({ycol} × {xcol})"`.

## Dependencies

`requirements.txt`:

```
numpy
pandas
plotly>=6.1.1
kaleido>=1.0
```

(matplotlib removed.)

`requirements-dev.txt`: unchanged.

`flake.nix` dev shell:

- Drop `matplotlib` from the Python package list.
- Add `plotly` and `kaleido` to the Python package list. If `kaleido`
  v1 is not yet in nixpkgs, document a `pip install kaleido>=1.0`
  fallback inside the dev shell.
- Add `pkgs.chromium` to system packages so kaleido finds a browser
  without user setup.

## Tests

`tests/python/conftest.py`:

- Drop the matplotlib import and `Agg` backend setup.
- Drop the `_close_mpl_figures` fixture.
- `make_args` gains `format=None`, `html_js="cdn"`, and `cmap=None` in
  the common defaults; surface/heatmap/facets gain `cmap=None` in
  their per-kind defaults.

Per-kind test files (`test_surface.py`, `test_heatmap.py`,
`test_facets.py`, `test_line.py`):

- Replace matplotlib axis assertions with plotly figure-dict
  assertions, e.g.:
  - `fig.data[0].type == "surface"`
  - `fig.layout.scene.xaxis.title.text == "branch_amount"`
  - `fig.data[0].colorscale[0][1].lower().startswith("#")` or assert
    the colorscale name on the trace.
  - For `--logz`: assert `surfacecolor` is pre-logged
    (e.g. `np.allclose(fig.data[0].surfacecolor, np.log10(...))`).
  - For facets: assert `len(fig.data) == n_facets` and
    `fig.layout.coloraxis.cmin/cmax` match the global range.

New `tests/python/test_output.py`:

- Extension inference: `.html` / `.png` / `.svg` / `.pdf` each route
  to the right writer.
- `--format` overrides extension.
- `--out` omitted → temp HTML created and `webbrowser.open` called
  (monkeypatch both).
- Unknown extension + no `--format` → `PlotError`.
- Chrome missing → `PlotError` with the install hint (monkeypatch
  `shutil.which` to return `None`).
- `--html-js` values map to `True | "cdn" | "directory"` in the
  `write_html` call (assert via a captured call shape).

Image-export tests in the per-kind files monkeypatch `fig.write_image`
to capture the call arguments rather than actually invoking kaleido;
this keeps the suite free of a Chrome dependency.

One integration test `tests/python/test_integration_export.py`, marked
`@pytest.mark.integration` and skipped when `shutil.which("chromium")`
and friends all return `None`, runs an end-to-end PNG export and
asserts the file exists and is non-empty.

## Error Handling

All existing `PlotError` paths are preserved:

- `<2` varying axes for heatmap/facets/surface → unchanged message
  ("at least 2 varying axis columns").
- Invalid `--x` / `--y` → unchanged messages.
- `--logz` with non-positive values → unchanged message ("--logz
  requires positive metric values").
- Mixed-benchmark CSVs → unchanged message.
- Missing grid cells (surface only) → unchanged message.

New `PlotError` paths:

- Chrome not found on first image export.
- `--out` with an unrecognized extension and no `--format`.
- `--cmap=NAME` where `NAME` is not a valid plotly colorscale
  (validate against the names exposed by `plotly.colors.named_colorscales()`
  before passing to the trace; on miss raise `PlotError` listing the
  available scales).

## Documentation

`README.md`: update the Quickstart example to show one HTML output
(`--out=/tmp/btb.html`) alongside PNG, and add a one-line note that
Chrome is required for PNG/SVG/PDF export and is provided by the Nix
dev shell.

`docs/cli.md`: document `--format`, `--html-js`, `--cmap` under the
common flags section. Add a short "Output formats" subsection
explaining extension inference and the no-`--out` browser fallback.

## Out Of Scope

- Custom lighting tuning per benchmark — defaults are fixed.
- Non-Chrome browser backends for kaleido — assume Chromium-class.
- Contour overlays on the surface plot.
- Animation, transitions, frame sequences.
- Dash apps or server-side rendering.
- Image-diff regression testing (figure-dict assertions are enough).

## Acceptance Criteria

- All four `python3 scripts/plot.py KIND ...` invocations work with
  the existing CSV inputs used in `tests/python/`.
- `--out=foo.png` produces a static image via kaleido.
- `--out=foo.html` produces an interactive HTML file. Default
  `--html-js=cdn` keeps file size small; `--html-js=inline` produces a
  self-contained file usable offline.
- No `--out` opens an interactive HTML in the system browser.
- Surface plot shows smooth per-pixel color across faces, not the
  per-quad flat shading of matplotlib's `plot_surface`.
- 2D plots (`line`, `heatmap`) show exact-value hover tooltips when
  opened as HTML.
- `requirements.txt` and `flake.nix` no longer reference matplotlib.
- The Python test suite covers the new behavior and passes.
- Chrome-missing path raises a clear `PlotError` rather than letting
  kaleido's internal error surface.

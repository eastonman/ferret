# CLI Reference

`ferret run <name>` accepts a fixed set of global flags. Each
benchmark contributes its own axes and per-bench options on top; see
the corresponding [`benchmarks/<name>.md`](benchmarks/) page for
those.

## Global flags

```
ferret run <name> [options] [--<axis>=value-or-range] [--<benchmark-option>=v]
  --out=PATH        CSV output (default stdout)
  --core=N          pin measurement thread to core N
  --freq=4.521GHz   running frequency, enables cycle columns
  --reps=K          repetitions per param point (default 7)
  --warmup=W        un-timed calls before measurement (default 1)
  --log-level=L     trace|debug|info|warn|error|critical|off (default warn)
  --seed=S          RNG seed for benchmarks that randomize (default 1)
```

## Axis syntax

Axes are sweep dimensions. Each benchmark declares its axes; the
runner produces one CSV row per cartesian-product point.

| form                | meaning                                                    |
| ------------------- | ---------------------------------------------------------- |
| `--<axis>=v`        | scalar — a single point on the axis                        |
| `--<axis>=v1,v2`    | explicit list — sweeps the listed values                   |
| `--<axis>=lo..hi`   | range — uses the axis's declared step policy               |
| `--<axis>=lo..hi@k` | `geom_range` axes only: override samples-per-octave to `k` |

The step policy is set by the axis declaration: `Axis::range` steps
by 1, `Axis::log2_range` steps by powers of two, `Axis::geom_range`
steps geometrically with `k` samples per octave (`k=1` is identical
to `log2_range`), `Axis::values` is list-only.

## Options vs axes

Options are scalar per-benchmark knobs — they appear as `--<opt>=v`
but are *not* swept. Every CSV row records the same option value.
Each benchmark page lists the options it accepts.

## Discovery

```sh
build/ferret list
```

Prints every registered benchmark name.

## Listing axes and options of a benchmark

Pass an unknown axis to surface the declared ones:

```sh
build/ferret run direct_branch_footprint --bogus=1
```

The runner reports the accepted axes and options before exiting.

## Plot subcommands

`scripts/plot.py` exposes four rendering kinds; each accepts a CSV
produced by `ferret run`.

```
python3 scripts/plot.py line     FILE.csv [--x=COL] [--xscale=log|linear] [--out=PATH] [--format=FMT] [--html-js=cdn|inline|sibling] [--metric=auto|cycles|ns] [--stat=min|median] [--ymax=N] [--series=COL,...]
python3 scripts/plot.py heatmap  FILE.csv [--x=COL] [--y=COL] [--out=PATH] [--format=FMT] [--html-js=cdn|inline|sibling] [--cmap=NAME] [--metric=auto|cycles|ns] [--stat=min|median] [--logz]
python3 scripts/plot.py surface  FILE.csv [--x=COL] [--y=COL] [--out=PATH] [--format=FMT] [--html-js=cdn|inline|sibling] [--cmap=NAME] [--metric=auto|cycles|ns] [--stat=min|median] [--logz] [--elev=DEG] [--azim=DEG]
python3 scripts/plot.py facets   FILE.csv --facet=COL [--x=COL] [--y=COL] [--out=PATH] [--format=FMT] [--html-js=cdn|inline|sibling] [--cmap=NAME] [--metric=auto|cycles|ns] [--stat=min|median] [--logz]
```

The plot script reads row 0 of the CSV's `benchmark` column to choose
sensible X/Y defaults per benchmark; pass `--x`/`--y` to override (or
`--benchmark=NAME` to force a registry lookup for a stripped CSV).
`line` defaults to a log-base-2 X axis (right for `log2_range` sweeps
like `branches` or `spacing_bytes`); pass `--xscale=linear` for plain
sweeps such as `nested_call_depth`'s `depth=1..64`, which the registry
also picks automatically. Use `surface` when you want the same two-axis
data as a 3D surface with color mapped from the selected metric. Use
`facets` with `--facet=COL` when a CSV has three or more varying axes
(future multi-parameter sweeps such as a TAGE capacity test).

## Output formats

Every plot kind shares these output flags:

- `--out=PATH`: write the figure to PATH. Extension determines the
  format unless `--format` overrides it. Supported extensions:
  `.html` (interactive), `.png`, `.svg`, `.pdf`, `.jpg`, `.webp`
  (all static images).
- `--format=html|png|svg|pdf|jpg|webp`: explicit format, overrides
  the extension on `--out`.
- `--html-js=cdn|inline|sibling` (default `cdn`):
  - `cdn` — HTML loads `plotly.js` from a CDN (~50 KB file, needs network on open).
  - `inline` — HTML includes a full copy of `plotly.js` (~5 MB file, works offline).
  - `sibling` — first HTML drops a `plotly.min.js` next to it; later HTMLs reuse it (good for output directories).

Omitting `--out` writes a temp HTML and opens it in the system browser.

Static image formats (PNG, SVG, PDF, JPG, WebP) require Chrome or
Chromium on PATH (kaleido uses headless Chrome for rendering). The
Nix dev shell provides `pkgs.chromium` on Linux; otherwise install
via your package manager. HTML output has no such requirement.

## Colorscale

`heatmap`, `facets`, and `surface` accept `--cmap=NAME` to choose a
plotly colorscale. Default is `turbo` (high-contrast perceptual)
for all three. Names are case-insensitive; the full list is at
<https://plotly.com/python/builtin-colorscales/>.

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

| form              | meaning                                          |
| ----------------- | ------------------------------------------------ |
| `--<axis>=v`      | scalar — a single point on the axis              |
| `--<axis>=v1,v2`  | explicit list — sweeps the listed values         |
| `--<axis>=lo..hi` | range — uses the axis's declared step policy     |

The step policy is set by the axis declaration: `Axis::range` steps
by 1, `Axis::log2_range` steps by powers of two, `Axis::values` is
list-only.

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

`scripts/plot.py` exposes three rendering kinds; each accepts a CSV
produced by `ferret run`.

```
python3 scripts/plot.py line     FILE.csv [--x=COL] [--xscale=log|linear] [--out=PATH] [--metric=auto|cycles|ns] [--stat=min|median] [--ymax=N] [--series=COL,...]
python3 scripts/plot.py heatmap  FILE.csv [--x=COL] [--y=COL] [--out=PATH] [--metric=auto|cycles|ns] [--stat=min|median] [--logz]
python3 scripts/plot.py facets   FILE.csv --facet=COL [--x=COL] [--y=COL] [--out=PATH] [--metric=auto|cycles|ns] [--stat=min|median] [--logz]
```

The plot script reads row 0 of the CSV's `benchmark` column to choose
sensible X/Y defaults per benchmark; pass `--x`/`--y` to override (or
`--benchmark=NAME` to force a registry lookup for a stripped CSV).
`line` defaults to a log-base-2 X axis (right for `log2_range` sweeps
like `branches` or `spacing_bytes`); pass `--xscale=linear` for plain
sweeps such as `nested_call_depth`'s `depth=1..64`, which the registry
also picks automatically. Use `facets` with `--facet=COL` when a CSV
has three or more varying axes (future multi-parameter sweeps such as
a TAGE capacity test).

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

# `store_load_overlap` — what counts as "the same access"

A serial dependent chain in which the load is deliberately misaligned
against the store it depends on, in address and in width. Per-link cost
is the forward latency plus `alu_ops`.

This asks what the fast path actually requires: whether an
overlapping-but-not-identical access qualifies, and whether the two
sides must be the same width.

## Kernel structure

```text
  xB = SP + slot                       hoisted out of the loop

  loop_top:
    ┌─ one link ───────────────────────────────────────────────┐
    │   str  x0, [xB, #0]              store_width bytes       │
    │   ldr  x3, [xB, #load_offset]    load_width bytes        │
    │   add  x0, x3, #1                                        │
    │   <alu_ops-1 further adds>                               │
    └──────────────────────────────────────────────────────────┘
    SUB counter, 1 ; JNZ loop_top

           store writes:  [ 0 .. store_width )
           load reads:    [ load_offset .. load_offset+load_width )
```

Annotated:

- **Addressing is through `xB`, which points AT the slot**, so the
  store's offset is `0` and the load's is just `load_offset`. Addressing
  from `SLJIT_SP` instead would make the load's offset `576+delta` —
  unaligned and past the 256-byte unscaled `LDUR`/`STUR` window — so
  sljit would prepend an address computation and the link would no
  longer be a fixed instruction count. `verify_layout` catches that.
- **The slot sits mid-cache-line.** A negative offset from a
  line-aligned slot would cross a 128-byte boundary, and a line split
  costs several times the forwarding penalty — enough to swamp the
  effect under test entirely.

## The byte-0 rule, and why points get rejected

The chain is carried by adding `1`, so **only byte 0 of the stored value
is guaranteed to change on every link**. A load that misses byte 0 reads
an effectively constant value; the dependency dies; the loop runs at
issue throughput. That reads _fast_ and means nothing.

So `validated_params` rejects any point where the load does not cover
byte 0 — formally `-load_width < load_offset <= 0`. This is not
fussiness: without it the benchmark silently reports a throughput number
in a latency column, which is the most dangerous kind of wrong.

The default axes are chosen so every combination in the rectangle is
valid at every width pair, because **a rejected point aborts the whole
sweep rather than being skipped**.

## Per-benchmark options

`--alu_ops=N` (default `1`, minimum `1`). One op is the floor: it
carries the loaded value back to the store's source register without the
two registers coinciding.

## CLI surface

| flag                        | meaning                                              |
| --------------------------- | ---------------------------------------------------- |
| `--load_offset_bytes=v1,v2` | Load address minus store address. Default `-2,-1,0`. |
| `--store_width=4,8`         | Store access width in bytes. Default sweeps both.    |
| `--load_width=4,8`          | Load access width in bytes. Default sweeps both.     |
| `--alu_ops=N`               | See above.                                           |

See [`../cli.md`](../cli.md) for global flags.

## Reading the curves

| Observation                                           | Reading                                                                    |
| ----------------------------------------------------- | -------------------------------------------------------------------------- |
| Only `offset=0` with matched widths is fast           | The fast path needs a byte-exact match; overlap is not enough.             |
| A load fully contained in the store is still slow     | Containment does not qualify either — the accesses must be identical.      |
| Matched 4-byte pair slow while matched 8-byte is fast | The mechanism is specific to full register width, not to width _matching_. |
| Everything flat                                       | Forwarding is address-range based, as a store queue would do.              |

The width axes are what separate "must match each other" from "must both
be 64-bit" — a matched narrow pair distinguishes the two, and only
sweeping both widths independently exposes it.

## Caveats

- **The chain must never load a repeating value.** A per-PC last-value
  load predictor will sever the chain and the row will report issue
  throughput as a latency -- roughly 0.4 cycles per link instead of the
  real number. The `alu_ops` carry increment is what keeps every load
  result fresh; see the dependent-chain section of
  [`../writing-a-benchmark.md`](../writing-a-benchmark.md) and validate
  with `scripts/chain_slope.py`.
- **The first parameter point of a sweep reads high with `--warmup=1`.**
  Use `--warmup=2` or more; the CI runner does.
- **Offsets are restricted to the byte-0 window** for the reason above.
  Wider offsets are reachable from the CLI and will be rejected, loudly,
  rather than measured.
- **Only 4- and 8-byte accesses.** Narrower widths would need a
  different chain-carry scheme to keep the dependency alive.
- **The layout check is AArch64-only** — x86_64 displacement width varies
  with the offset, so a uniform per-link stride does not hold there.
- **Apple Silicon pinning.** See the project README's discipline section.

## Related docs

- Companion benchmarks: [`store_load_footprint.md`](store_load_footprint.md),
  [`store_load_distance.md`](store_load_distance.md).
- Construction rationale: [`writing-a-benchmark.md`](../writing-a-benchmark.md).

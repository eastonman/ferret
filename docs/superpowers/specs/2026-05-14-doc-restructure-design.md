# Doc restructure — per-benchmark pages and README split

## Motivation

The current `README.md` (215 lines) mixes project pitch, build, sanitizer
matrix, usage, CLI reference, per-benchmark blurbs, formatting/linting,
and discipline into one file. Per-benchmark information for
`nested_call_depth` lives in `docs/benchmarks/nested_call_depth.md`; the
other two benchmarks have no dedicated page and are described only as
table rows + a paragraph blurb in the README. As more benchmarks land
this pattern doesn't scale — the README grows, and there's no consistent
place for each benchmark's kernel structure, options, and caveats.

Goal: split the README along audience lines, give every benchmark its
own page following a common skeleton, and include an ASCII diagram per
benchmark showing the structure that the JIT actually emits.

## Non-goals

- No code changes. This is a pure documentation refactor.
- No new content beyond what's already implied by the existing README +
  source. ASCII diagrams are derived from reading the existing
  `benchmarks/*.cpp` emitters; per-benchmark options are restated from
  current `BenchOptions`/axis declarations.
- No changes to `docs/architecture.md`, `docs/writing-a-benchmark.md`,
  or anything under `docs/superpowers/`.

## Target file layout

```
README.md                                  (slimmed)
docs/
├── README.md                              (index — updated)
├── architecture.md                        (unchanged)
├── writing-a-benchmark.md                 (unchanged)
├── build.md                               (NEW)
├── cli.md                                 (NEW)
├── contributing.md                        (NEW)
└── benchmarks/
    ├── dependent_chain_throughput.md      (NEW)
    ├── direct_branch_footprint.md         (NEW)
    └── nested_call_depth.md               (UPDATED — adds ASCII, drops two-step block)
```

### README.md (slimmed)

User-facing landing page. Contains:

1. Project pitch (first 2 paragraphs of current README).
2. Supported-platforms table.
3. **Quickstart** — a five-line build snippet (Nix path) and a pointer
   to `docs/build.md` for full options.
4. **Two-step cycle workflow** — kept verbatim as the runnable
   hello-world. It teaches the unit-discipline (cycles vs. ns,
   probe-then-run) that every user needs to internalize once.
5. **Benchmark index** — a table with columns: name, target structure,
   link to its `docs/benchmarks/<name>.md` page.
6. **Discipline** — kept verbatim. This is the section that gets
   ignored if it's not on the landing page.
7. **Docs** — a short link list pointing to `docs/README.md`,
   `docs/architecture.md`, `docs/writing-a-benchmark.md`,
   `docs/cli.md`, `docs/build.md`, `docs/contributing.md`.

Removed from README (moved out): build dependency requirements; full
build recipes; sanitizer matrix; CLI flag table; per-benchmark option
blurbs; formatting and linting section.

### docs/build.md (new)

Contains everything currently under README's "Build" heading:

- Dependency requirements
- Nix recipe
- Plain CMake recipe
- Sanitizer builds matrix and recipe (including the macOS sanitizer
  caveat).

### docs/cli.md (new)

Contains the global CLI reference currently under README's "CLI flags"
heading:

- The `ferret run <name> [options] [--<axis>=…]` flag table (out, core,
  freq, reps, warmup, log-level, seed).
- Axis syntax: scalar (`--depth=8`), list (`--depth=8,16,32`), range
  (`--depth=1..32`).
- Note on the distinction between axes and options (axes sweep; options
  are scalar overrides). Link to per-benchmark pages for the axes and
  options each benchmark accepts.

### docs/contributing.md (new)

Contains everything currently under README's "Formatting and linting"
heading: the `clang-format`/`clang-tidy`/`ruff` matrix, the
`./scripts/format.sh` and `./scripts/lint.sh` recipes, the CI-parity
build command, and a pointer to `writing-a-benchmark.md` for adding
benchmarks.

### docs/benchmarks/<name>.md (per-benchmark template)

Common skeleton, sections optional. Headings appear in this order when
present:

1. **Intro** — one paragraph: what microarchitectural structure this
   probes; what the curve looks like; what the cliff/inflection means.
2. **Kernel structure** — the ASCII diagram (see next section), plus a
   short paragraph annotating the parts.
3. **Variants / per-benchmark options** — only when applicable.
4. **CLI surface** — table of axes + options this benchmark accepts,
   with a "see [`cli.md`](../cli.md) for global flags" pointer.
5. **Reading the curves** — what to expect; a real example table when
   one exists.
6. **Caveats specific to this benchmark** — only when there are any.
7. **Related docs** — links to specs, plans, external references.

The two-step workflow does **not** appear on per-benchmark pages — it's
on the README. Per-benchmark pages may show a concrete `ferret run`
invocation inline within "Reading the curves" if useful, but the
probe-then-run protocol is not restated.

## ASCII diagrams (one per benchmark)

Each diagram depicts the structure most informative for that benchmark.
A single template doesn't fit all three: dependent_chain is a dataflow
chain, direct_branch is a spatial layout, nested_call is a nested call
tree. The choice is per-benchmark.

The diagrams must be **derived from the actual emission code** in
`benchmarks/*.cpp` (registers, branch directions, body shape), not from
authorial intent. The plan step will cross-check each diagram against
the corresponding `emit_kernel()`.

### dependent_chain_throughput — register dependency chain

```
 ┌──────────────┐
 │ ADD  x, x, 1 │  ← all N ops write x, read x
 └──────┬───────┘
        │ RAW on x
        ▼
 ┌──────────────┐
 │ ADD  x, x, 1 │
 └──────┬───────┘
        │
        ▼
       ...   (chain_length nodes, single live register)
        │
        ▼
       RET
```

Annotated: emitted as a 1024-ADD unrolled inner loop + a straight-line
tail of `chain_length % 1024` ADDs. All ADDs target the same register
(`SLJIT_R0`); each reads the previous result, so the chain serializes
at ADD latency (1 cycle on every common high-perf core).

### direct_branch_footprint — code layout in memory

```
   PC                site
 0x0000   ┌──────────────────────┐
          │  B   target_0        │ ──┐
          │  <pad to spacing>    │   │
 0x0040   ├──────────────────────┤   │  spacing_bytes (default 64)
          │  B   target_1        │ ──┼─┐
          │  <pad to spacing>    │   │ │
 0x0080   ├──────────────────────┤   │ │
          │  B   target_2        │ ──┼─┼─┐
          │   ...                │   │ │ │
          ├──────────────────────┤   │ │ │
          │  exit label          │   │ │ │
          └──────────────────────┘   │ │ │
                                     │ │ │
   sattolo_permute=0 (default):      │ │ │
     target_i = site_{i+1}     ◄─────┘─┘─┘   sequential fall-through chain
   sattolo_permute=1:
     target_i = π(i)                          Hamiltonian cycle (Sattolo)
                                              breaks spatial I-cache prefetch
```

Annotated: N branch sites at PC = base + i × spacing, each site being a
branch encoding (4 B on AArch64, 5 B on x86_64) followed by NOP padding
to `spacing`. Default wiring is the sequential chain; `sattolo_permute=1`
rewires targets into a random Hamiltonian cycle to isolate BTB from
sequential-prefetch / I-cache spatial-locality effects. The cycle's
unique edge to label[0] is rerouted to the exit label so each iteration
executes exactly N branches.

### nested_call_depth — nested call tree

Variant 1 (default) is the canonical picture; variants 0 and 2 differ
in fan-out per body.

```
 body_0:                     body_1:                     body_{N-1}:
 ┌───────────────────┐       ┌───────────────────┐       ┌───────────────────┐
 │ AND  ctr, 1       │       │ AND  ctr, 1       │       │     (leaf)        │
 │ JZ   site_a       │       │ JZ   site_a       │       │     RET           │
 │ site_b: CALL b1   │  ──▶  │ site_b: CALL b2   │  ...  └─────────┬─────────┘
 │ JMP  end          │       │ JMP  end          │                 │
 │ site_a: CALL b1   │  ──▶  │ site_a: CALL b2   │                 │
 │ end:              │       │ end:              │                 │
 │ RET               │  ◀──  │ RET               │  ◀── ... ◀──────┘
 └───────────────────┘       └───────────────────┘
        ↑                           K=2 sites per body
        │                           dispatch on ctr bit 0
   outer loop                       (perfectly predicted after first iter)
```

Variant 0: each body has a single CALL site (no AND/JZ); the diagram
collapses to one column per body. Variant 2: each body has eight CALL
sites and a three-CB binary tree dispatching on a byte from
`path_table[row][i]`; the diagram shows the K=8 fan-out and the
path-table load.

Annotated: depth-N nested call chain; per-body fan-out (1 / 2 / 8) is
what the `--variant` axis selects. The outer loop drives `ctr` (or the
path-table row) so each ret PC sees a multi-target return-address
pattern across iterations, defeating last-target-per-PC indirect
predictors.

## Cross-link conventions

- README → per-benchmark page via the benchmark-index table.
- README → global CLI flags via `docs/cli.md` link.
- Per-benchmark page → `docs/cli.md` for global flags; → existing
  `docs/superpowers/specs/…` for design/plan references it already
  cites.
- `docs/README.md` index lists every doc with a one-line hook.

## Spec-to-content checks

The plan step will verify, before declaring the refactor complete:

- Every link in the new docs resolves (no dangling
  `docs/benchmarks/X.md` references).
- The slimmed README still contains the discipline section and the
  two-step cycle workflow.
- Each per-benchmark page lists every axis and option the corresponding
  `Benchmark::axes()` / `Benchmark::options()` declares.
- `docs/README.md` index includes pointers to the three new top-level
  docs and the two new benchmark pages.
- ASCII diagrams match the kernel each `emit_kernel()` actually
  produces.

## Out of scope

- Refining or rewriting any of the existing prose beyond the structural
  move. The migration is cut-and-paste, not a copyedit.
- Adding new benchmarks or new options.
- Changing the `docs/superpowers/` layout.

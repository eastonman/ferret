# Ferret Documentation

## Live documentation

These docs reflect current behavior. Edit them when behavior changes.

- [`../AGENTS.md`](../AGENTS.md) — operational checklist for contributors and agentic workers (pre-PR commands, conventions, CI gates, footguns).
- [`architecture.md`](architecture.md) — codebase overview and module map.
- [`build.md`](build.md) — dependency requirements, Nix/CMake recipes, sanitizer matrix, single-test recipes.
- [`cli.md`](cli.md) — global `ferret run` flags and axis syntax.
- [`writing-a-benchmark.md`](writing-a-benchmark.md) — guide for adding a new benchmark.
- [`contributing.md`](contributing.md) — formatters and linters (points to AGENTS.md).
- [`../README.md`](../README.md) — user-facing pitch, quickstart, two-step cycle workflow, discipline.

### Benchmarks

One page per benchmark, with kernel-structure ASCII, CLI surface, and reading-the-curves notes.

- [`benchmarks/dependent_chain_throughput.md`](benchmarks/dependent_chain_throughput.md) — frequency-probe baseline.
- [`benchmarks/direct_branch_footprint.md`](benchmarks/direct_branch_footprint.md) — direct-jump BTB capacity.
- [`benchmarks/nested_call_depth.md`](benchmarks/nested_call_depth.md) — RAS depth.
- [`benchmarks/branch_history_footprint.md`](benchmarks/branch_history_footprint.md) — conditional-branch direction-predictor capacity.

## Historical artifacts

The `superpowers/specs/` and `superpowers/plans/` directories hold
point-in-time design and implementation records. They are kept for
git-archaeology context — to see what we were thinking and what we
chose not to do at a given moment.

**They are not current documentation.** If a spec disagrees with the
code, the code is right and the spec is frozen.

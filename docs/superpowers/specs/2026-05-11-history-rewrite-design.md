# Ferret v1 history rewrite — design

## Goal

Collapse the 33-commit `main` history into a clean 24-commit linear story where each commit is a functional, self-contained step from the original v1 implementation plan. Absorb the 8 `fix(round-N-*)` commits and the post-loop `refactor(simplify)` commit into the feature commits whose code they amended. The end result: every feature commit reflects the **final hardened state** of its subsystem, with no review-loop narrative left in the log.

## Why

The current history mixes implementation phases with the RLCR review feedback loop:

- 14 feature/infra/docs commits introducing subsystems (`bec21ab` → `ecb4534` plus build/spec/docs)
- 8 `fix(round-N-*)` commits each addressing one or more reviewer findings
- 1 `refactor(simplify)` Finalize Phase cleanup
- 1 `chore` for AI-agent gitignore

The round-N commits are valuable as a record of the loop, but they make the log hard to read as a description of what ferret v1 is. A future contributor or reader of the repo benefits more from "this commit introduces the runner with K<=0 validation" than from "round 1 hardened the runner after the original commit shipped without it".

The git history on `backup-v1-history` will preserve the full loop record indefinitely.

## Non-goals

- No code changes. The working tree at `HEAD` (`ee2f556`) is the desired final state and stays byte-for-byte identical after the rewrite.
- No changes to commits 1-7 (initial → spec → plan → cmake bootstrap → flake → cli11+sljit).
- No squashing across subsystem boundaries (e.g., we do not merge `feat(timing)` and `feat(pinning)` even though both are infra-ish).
- No commit-message rewriting style migration (we stay on Angular Conventional Commits, per the repo's existing convention).

## Strategy

Naive interactive rebase would not work: each round-N commit's `main.cpp` changes depend on round-(N-1)'s, so reordering fixes next to their target `feat` commit would produce many conflicts. Instead we **rebuild** the history:

1. **Backup.** Create `backup-v1-history` branch at current `HEAD`. Optionally push it to `origin/backup-v1-history` so the loop record is preserved off-machine.
2. **Reset.** `git reset --hard 36b7882` — this drops back to the last build-infra commit. Commits 1-7 remain.
3. **Replay.** For each new commit in the target structure (see next section), gather the appropriate files at the appropriate state (mostly from `ee2f556`, sometimes from the original feat commit) and create one fresh commit. The working tree gets rebuilt step by step.
4. **Verify.** After each feature/test commit, run `cmake --build build && ctest --output-on-failure` to confirm the commit is independently buildable and passes its test suite. The build directory is reused (incremental) so total wall-clock stays under ~25 min.
5. **Review.** Show the user the new `git log` before pushing. No `git push --force-with-lease` until the user explicitly approves.

### CMakeLists handling

`CMakeLists.txt` and `tests/CMakeLists.txt` evolve incrementally — every original `feat` commit added entries for its new sources/tests. No fix commit modifies top-level `CMakeLists.txt`. Only `6f65832` (round 6) modifies `tests/CMakeLists.txt` (to add `PROPERTIES TIMEOUT 30` for `test_integration`).

Therefore:

- For steps 8-22, `CMakeLists.txt` and `tests/CMakeLists.txt` come from the **original `feat` commit at that step**, capturing the cumulative state at that point.
- For step 23 (integration tests), `tests/CMakeLists.txt` comes from `ee2f556` (final, includes `TIMEOUT 30`).

## Target structure

Commits 1-7 unchanged. Steps 8-24:

| # | Subject | Source files (final state from `ee2f556` unless noted) | CMakeLists source | Absorbs |
|---|---------|---|---|---|
| 8 | `feat(core): add params and axis primitives` | `include/ferret/params.hpp`, `include/ferret/axis.hpp`, `src/axis.cpp`, `tests/test_params_axis.cpp` | `ecb4534` | 08ce456, 2567ec7, 54d0496, be4b5cf |
| 9 | `feat(core): add sweep::expand for axis cross-product with overrides` | `include/ferret/sweep.hpp`, `src/sweep.cpp`, `tests/test_sweep.cpp` | `e81ad07` | 06a2c64 |
| 10 | `feat(cli): add axis-value parser for ranges and value lists` | `include/ferret/cli_axis.hpp`, `src/cli_axis.cpp`, `tests/test_cli_axis.cpp` | `c411200` | 08ce456, 2567ec7, be4b5cf |
| 11 | `feat(timing): add per-arch tick counter and ticks-per-ns calibration` | `include/ferret/timing.hpp`, `src/timing/aarch64.cpp`, `src/timing/calibrate.cpp`, `src/timing/x86_64.cpp`, `tests/test_timing.cpp` | `ed84c37` | — |
| 12 | `feat(pinning): add best-effort core pin, priority, mlock` | `include/ferret/pinning.hpp`, `src/pinning/linux.cpp`, `src/pinning/macos.cpp`, `tests/test_pinning.cpp` | `9e5eb15` | — |
| 13 | `feat(core): add benchmark base class and static registry` | `include/ferret/benchmark.hpp`, `src/benchmark_registry.cpp`, `tests/test_benchmark_registry.cpp` | `e1396f0` | — |
| 14 | `feat(output): add csv writer with optional cycles columns` | `include/ferret/csv.hpp`, `include/ferret/runner.hpp`, `src/output/csv.cpp`, `tests/test_csv.cpp` | `defecd7` | — |
| 15 | `feat(runner): add measurement loop with warmup and min-of-K` | `src/runner.cpp`, `tests/test_runner.cpp` | `d0223c7` | 08ce456 (K<=0 / warmup<0 throws) |
| 16 | `feat(cli): add main with run/list subcommands` | `src/main.cpp` (final: parse_freq with finite check + `fail` lambda, --reps/--warmup pre-flight, buffer-then-flush, tpns guard, zero-work guard, sweep::expand + benchmark try/catch with config/sweep/benchmark error paths) | `6a6c237` | 08ce456, 2567ec7, aec34d0 (parse_freq), 1d8e785, 51c380c, 54d0496, 06a2c64, be4b5cf (fail lambda + comment trim) |
| 17 | `feat(bench): add dependent_chain_throughput frequency probe` | `benchmarks/dependent_chain_throughput.cpp` (final: exact op count via `full_blocks` + remainder tail) | `7a2947e` | aec34d0 (exact op count) |
| 18 | `feat(core): add per-arch nop padding helper` | `include/ferret/padding.hpp`, `src/padding.cpp`, `tests/test_padding.cpp` | `9607b27` | — |
| 19 | `feat(bench): add direct_branch_footprint primary benchmark` | `benchmarks/direct_branch_footprint.cpp` | `bec21ab` | — |
| 20 | `feat(scripts): add freq.py and plot.py helpers` | `scripts/freq.py`, `scripts/plot.py` | n/a | — |
| 21 | `docs: write user-facing readme` | `README.md` | n/a | — |
| 22 | `ci: add github actions matrix and nix job` | `.github/workflows/ci.yml` | n/a | — |
| 23 | `test: add end-to-end integration tests with CTest timeout` | `tests/test_integration.cpp` (all integration tests merged: malformed CLI, mixed-sweep buffer-flush, freq edge, zero-work, negative unsigned, empty axis), `tests/CMakeLists.txt` with `PROPERTIES TIMEOUT 30` | `ee2f556` | 9140f07, 08ce456, 2567ec7, aec34d0, 1d8e785, 51c380c, 54d0496, 6f65832 |
| 24 | `chore: add ai agent artifacts to gitignore` | `.gitignore` | n/a | — |

### Build invariants enforced

- Step 8 introduces `expand_log2_range` helper in `axis.hpp`; step 10 (`cli_axis`) consumes it. Both compile in order.
- Step 14 introduces `include/ferret/runner.hpp` (it was added at the CSV writer step originally because `CsvWriter` consumes `MeasurementRow`). Step 15 only adds `src/runner.cpp` + tests.
- Step 16 (main) lands last among code commits because final `main.cpp` depends on every subsystem 8-15.
- Step 23 (integration tests) lands after main + benchmarks so the binary under test exists.

## Commit-message style

Subject lines stay as listed above (Angular Conventional Commits, matching the repo's existing convention from `feedback_commit_style`).

Bodies describe the **final subsystem behavior**, not the round history. Example for step 16:

> Implements the `ferret run <bench>` and `ferret list` subcommands. Parses
> `--freq`/`--reps`/`--warmup` with finite-value validation, applies CLI axis
> overrides, expands the sweep cross-product, and drives the runner across every
> param point. Buffers all rows in memory and only writes the CSV after the full
> sweep succeeds — so on any benchmark or sweep failure, the output file/stdout
> stays empty and exit code is 2 with a `ferret: ...` message to stderr.
> Rejects degenerate (zero-work) params and non-finite `ticks_per_ns`
> calibration up front.

Keep the existing `Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>` trailer on each feature commit, matching the convention used throughout the existing v1 history.

## Verification plan

After each of the 17 reconstructed commits (steps 8-24, minus pure-doc/script/CI/gitignore steps 20-22, 24 which can't break the build):

```sh
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

Expected outcomes:

- Steps 8-15: only the tests for that subsystem (and earlier ones) run; all pass.
- Step 16 (main): the `ferret` binary builds; same test set passes.
- Step 17 + 19: benchmark sources compile; tests still pass.
- Step 18 (padding): `test_padding` joins the suite; all pass.
- Step 23: full integration test suite runs; final test count matches `ee2f556` (69 tests pass + 1 skipped on aarch64 host per `feedback_review_repros` template).

If any commit fails to build or test, halt and re-examine the mapping for that commit (likely a missed file or a stale CMakeLists entry).

## Final verification at `HEAD`

After all 24 commits are in place:

1. `git diff backup-v1-history` — should be empty (working tree identical to original `HEAD`).
2. `cmake --build build --clean-first && ctest --output-on-failure` — clean rebuild from scratch passes.
3. `git log --oneline` — visual review of the new history.

## Push plan

After the user reviews the new log:

1. `git push origin backup-v1-history` (preserves loop record off-machine).
2. `git push --force-with-lease origin main` (the `--force-with-lease` flag rejects the push if `origin/main` has moved since the last fetch, which is a small safety net on a single-contributor repo).

Pushing happens only on explicit user approval.

## Risks

1. **Working-tree diff between `backup-v1-history` and final rebuilt `HEAD` must be empty.** This is the single load-bearing invariant. The verification step explicitly checks it.
2. **CMakeLists drift.** If a fix commit accidentally added a source file to `CMakeLists.txt` (it didn't, per our audit), we'd miss it. The full-rebuild verification at the end would catch this as a link error.
3. **Force-pushing a published branch.** The remote is single-contributor; impact is limited to any open clones. Backup branch on remote mitigates fully.
4. **Token in remote URL** (`https://oauth2:ghp_...@github.com/...`). Not a rewrite concern but worth flagging: the user should consider rotating the PAT and switching to credential helper or SSH at some point. Not blocking this work.

## Open questions

None. All clarifications resolved during brainstorming:

- Granularity: per-subsystem (option A), 24 commits total.
- Backup branch: `backup-v1-history`, locally first; also pushed to remote during the push phase.
- Per-commit verification: yes, ctest after each feature/test commit.
- Push timing: only after user reviews the new log.

## Next step

Invoke `writing-plans` skill to produce an execution plan (one task per commit, with exact `git` and `ctest` invocations) from this spec.

# Ferret v1 History Rewrite Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite `main` from 33 commits + 1 spec commit into 24 clean per-subsystem commits plus the spec commit at the tip, with all 8 round-N review-fix commits and the Finalize Phase refactor absorbed into the feature commits whose code they amended.

**Architecture:** Rebuild-not-rebase. Backup current `main` to `backup-v1-history`, hard-reset `main` to the last build-infra commit (`36b7882`), then for each new commit grab the final hardened state of its source files from `ee2f556` plus the cumulative CMakeLists state from the original feat commit. Verify with `ctest` after each feature/test commit. Final invariant: `git diff backup-v1-history` must be empty after cherry-picking the spec doc back on the tip.

**Tech Stack:** Git, CMake, CTest (GoogleTest harness).

---

## File Structure

No new source/test files created or removed by this plan. The plan only rearranges existing commits. Files touched (all already present in the working tree):

- `include/ferret/*.hpp`, `src/**/*.cpp`, `benchmarks/*.cpp`, `tests/*.cpp` — moved as a set to specific new commits
- `CMakeLists.txt`, `tests/CMakeLists.txt` — picked from the original feat commit at each step (cumulative state up to that point)
- `scripts/*.py`, `README.md`, `.github/workflows/ci.yml`, `.gitignore` — moved to their original-style commits
- `docs/superpowers/specs/2026-05-11-history-rewrite-design.md` — already committed as `ccfecda`; will be cherry-picked back onto the rewritten history as the tip commit

## Reference: commit-SHA → new-step map

| New step | Subject | Source SHAs to checkout |
|----------|---------|-------------------------|
| 8 | `feat(core): add params and axis primitives` | CMakeLists from `ecb4534`; source from `ee2f556` |
| 9 | `feat(core): add sweep::expand for axis cross-product with overrides` | CMakeLists from `e81ad07`; source from `ee2f556` |
| 10 | `feat(cli): add axis-value parser for ranges and value lists` | CMakeLists from `c411200`; source from `ee2f556` |
| 11 | `feat(timing): add per-arch tick counter and ticks-per-ns calibration` | CMakeLists from `ed84c37`; source from `ed84c37` (unchanged) |
| 12 | `feat(pinning): add best-effort core pin, priority, mlock` | CMakeLists from `9e5eb15`; source from `9e5eb15` (unchanged) |
| 13 | `feat(core): add benchmark base class and static registry` | CMakeLists from `e1396f0`; source from `e1396f0` (unchanged) |
| 14 | `feat(output): add csv writer with optional cycles columns` | CMakeLists from `defecd7`; source from `defecd7` (unchanged) |
| 15 | `feat(runner): add measurement loop with warmup and min-of-K` | CMakeLists from `d0223c7`; source from `ee2f556` |
| 16 | `feat(cli): add main with run/list subcommands` | CMakeLists from `6a6c237`; source from `ee2f556` |
| 17 | `feat(bench): add dependent_chain_throughput frequency probe` | CMakeLists from `7a2947e`; source from `ee2f556` |
| 18 | `feat(core): add per-arch nop padding helper` | CMakeLists from `9607b27`; source from `9607b27` (unchanged) |
| 19 | `feat(bench): add direct_branch_footprint primary benchmark` | CMakeLists from `bec21ab`; source from `bec21ab` (unchanged) |
| 20 | `feat(scripts): add freq.py and plot.py helpers` | source from `18c1567` (unchanged) |
| 21 | `docs: write user-facing readme` | source from `6c1226b` (unchanged) |
| 22 | `ci: add github actions matrix and nix job` | source from `3845da5` (unchanged) |
| 23 | `test: add end-to-end integration tests with CTest timeout` | tests/CMakeLists from `ee2f556`; source from `ee2f556` |
| 24 | `chore: add ai agent stuff into gitignore` | source from `ee2f556` (unchanged) |

For "unchanged" rows, the new commit message is also reused via `git commit -C <orig_sha>`. For all other rows, the plan provides the full new message text.

---

### Task 1: Backup current main to a recovery branch

**Files:** none.

- [ ] **Step 1: Verify clean working tree and current HEAD**

```bash
git status
git log -1 --format="%H %s"
```

Expected: `nothing to commit, working tree clean`. HEAD is `ccfecda docs(spec): add v1 history rewrite design`.

- [ ] **Step 2: Create local backup branch**

```bash
git branch backup-v1-history
```

Expected: silent success.

- [ ] **Step 3: Verify backup branch points at current HEAD**

```bash
git log -1 --format="%H %s" backup-v1-history
```

Expected: same SHA and subject as current HEAD (`ccfecda docs(spec): add v1 history rewrite design`).

- [ ] **Step 4: Push backup branch to origin (preserves loop record off-machine)**

```bash
git push -u origin backup-v1-history
```

Expected: new branch created on origin.

---

### Task 2: Reset main to the last build-infra commit

**Files:** working tree contents will shrink to the state at `36b7882`.

- [ ] **Step 1: Hard-reset main to 36b7882**

```bash
git reset --hard 36b7882
```

Expected: `HEAD is now at 36b7882 build: wire cli11 and sljit via find_package then fetchcontent`.

- [ ] **Step 2: Verify working tree matches 36b7882**

```bash
git log --oneline | head -3
git status
ls include/ 2>/dev/null; ls benchmarks/ 2>/dev/null
```

Expected: only 7 commits in log (`36b7882` at top). No `include/ferret/` source headers, no `benchmarks/` files. Working tree clean.

- [ ] **Step 3: Smoke-build and test current state**

```bash
cmake -S . -B build
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

Expected: build succeeds, smoke + deps_link tests pass. This confirms the base is sound before we start replaying.

---

### Task 3: Replay step 8 — params + axis primitives

**Files:**
- Create: `include/ferret/params.hpp`, `include/ferret/axis.hpp`, `src/axis.cpp`, `tests/test_params_axis.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Stage source files from final state**

```bash
git checkout ee2f556 -- include/ferret/params.hpp include/ferret/axis.hpp src/axis.cpp tests/test_params_axis.cpp
```

- [ ] **Step 2: Stage CMakeLists from original feat commit (cumulative state at this step)**

```bash
git checkout ecb4534 -- CMakeLists.txt tests/CMakeLists.txt
```

- [ ] **Step 3: Verify staging**

```bash
git status
```

Expected: 6 files staged (4 new sources + 2 CMakeLists). No unexpected entries.

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(core): add params and axis primitives

Params is a key/value bag of int64 with type-discriminated getters:
Params::get<T> uses if constexpr to reject negative values when T is
unsigned. Axis is a typed sweep dimension with three kinds (single
value, log2 range, value list). Axis::expand uses a shared
expand_log2_range helper (declared in axis.hpp) that validates lo > 0
and guards against signed-overflow when doubling.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 5: Verify build + tests**

```bash
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

Expected: build succeeds; `test_params_axis` + smoke + deps_link pass.

---

### Task 4: Replay step 9 — sweep::expand

**Files:**
- Create: `include/ferret/sweep.hpp`, `src/sweep.cpp`, `tests/test_sweep.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Stage source files**

```bash
git checkout ee2f556 -- include/ferret/sweep.hpp src/sweep.cpp tests/test_sweep.cpp
```

- [ ] **Step 2: Stage CMakeLists from e81ad07**

```bash
git checkout e81ad07 -- CMakeLists.txt tests/CMakeLists.txt
```

- [ ] **Step 3: Verify staging**

```bash
git status
```

Expected: 5 files modified/staged.

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(core): add sweep::expand for axis cross-product with overrides

Takes declared axes plus optional CLI overrides keyed by axis name and
produces the full cross-product of param points. Resolves overrides
strictly: every declared axis must end with at least one value, so any
empty resolved axis (declared as empty or overridden to empty) throws
std::invalid_argument with the axis name.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 5: Verify build + tests**

```bash
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

Expected: `test_sweep` passes alongside earlier tests.

---

### Task 5: Replay step 10 — axis-value CLI parser

**Files:**
- Create: `include/ferret/cli_axis.hpp`, `src/cli_axis.cpp`, `tests/test_cli_axis.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Stage source files**

```bash
git checkout ee2f556 -- include/ferret/cli_axis.hpp src/cli_axis.cpp tests/test_cli_axis.cpp
```

- [ ] **Step 2: Stage CMakeLists from c411200**

```bash
git checkout c411200 -- CMakeLists.txt tests/CMakeLists.txt
```

- [ ] **Step 3: Verify staging**

```bash
git status
```

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(cli): add axis-value parser for ranges and value lists

Accepts CLI fragments like --branches=1..32768 (log2 range),
--branches=1,4,16 (value list), or --branches=64 (single). Range and
list/single branches share validate_value_against_kind so log2 axes
reject zero/negative values from every entry path. The log2 doubling
uses axis.hpp's expand_log2_range helper, sharing one source of truth
with Axis::expand for the lo > 0 check and signed-overflow guard.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 5: Verify build + tests**

```bash
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

---

### Task 6: Replay step 11 — timing (unchanged)

**Files:**
- Create: `include/ferret/timing.hpp`, `src/timing/aarch64.cpp`, `src/timing/calibrate.cpp`, `src/timing/x86_64.cpp`, `tests/test_timing.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Stage all files from original commit**

```bash
git checkout ed84c37 -- include/ferret/timing.hpp src/timing/aarch64.cpp src/timing/calibrate.cpp src/timing/x86_64.cpp tests/test_timing.cpp CMakeLists.txt tests/CMakeLists.txt
```

- [ ] **Step 2: Verify staging**

```bash
git status
```

- [ ] **Step 3: Commit (reuse original message)**

```bash
git commit -C ed84c37
```

- [ ] **Step 4: Verify build + tests**

```bash
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

---

### Task 7: Replay step 12 — pinning (unchanged)

**Files:**
- Create: `include/ferret/pinning.hpp`, `src/pinning/linux.cpp`, `src/pinning/macos.cpp`, `tests/test_pinning.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Stage all files from original commit**

```bash
git checkout 9e5eb15 -- include/ferret/pinning.hpp src/pinning/linux.cpp src/pinning/macos.cpp tests/test_pinning.cpp CMakeLists.txt tests/CMakeLists.txt
```

- [ ] **Step 2: Commit and verify**

```bash
git status
git commit -C 9e5eb15
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

---

### Task 8: Replay step 13 — benchmark base + registry (unchanged)

**Files:**
- Create: `include/ferret/benchmark.hpp`, `src/benchmark_registry.cpp`, `tests/test_benchmark_registry.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Stage all files from original commit**

```bash
git checkout e1396f0 -- include/ferret/benchmark.hpp src/benchmark_registry.cpp tests/test_benchmark_registry.cpp CMakeLists.txt tests/CMakeLists.txt
```

- [ ] **Step 2: Commit and verify**

```bash
git status
git commit -C e1396f0
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

---

### Task 9: Replay step 14 — CSV output writer (unchanged)

**Files:**
- Create: `include/ferret/csv.hpp`, `include/ferret/runner.hpp`, `src/output/csv.cpp`, `tests/test_csv.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Stage all files from original commit**

```bash
git checkout defecd7 -- include/ferret/csv.hpp include/ferret/runner.hpp src/output/csv.cpp tests/test_csv.cpp CMakeLists.txt tests/CMakeLists.txt
```

- [ ] **Step 2: Commit and verify**

```bash
git status
git commit -C defecd7
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

---

### Task 10: Replay step 15 — runner with measurement loop

**Files:**
- Create: `src/runner.cpp`, `tests/test_runner.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Stage source files from final state (with K<=0/warmup<0 throws)**

```bash
git checkout ee2f556 -- src/runner.cpp tests/test_runner.cpp
```

- [ ] **Step 2: Stage CMakeLists from d0223c7**

```bash
git checkout d0223c7 -- CMakeLists.txt tests/CMakeLists.txt
```

- [ ] **Step 3: Verify staging**

```bash
git status
```

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(runner): add measurement loop with warmup and min-of-K

Runs the JIT-compiled kernel iters times per repetition, K + warmup
times total, reads the per-arch tick counter around the inner call,
and returns a MeasurementRow with min, median, and all-K tick samples.
Throws std::invalid_argument if K <= 0 or warmup < 0 as a defensive
guard for any non-CLI caller bypassing the main.cpp pre-flight check.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 5: Verify build + tests**

```bash
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

Expected: `test_runner` passes including the new K<=0 / warmup<0 throw cases.

---

### Task 11: Replay step 16 — main with run/list subcommands

**Files:**
- Create: `src/main.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Stage source from final state**

```bash
git checkout ee2f556 -- src/main.cpp
```

- [ ] **Step 2: Stage CMakeLists from 6a6c237**

```bash
git checkout 6a6c237 -- CMakeLists.txt
```

- [ ] **Step 3: Verify staging**

```bash
git status
```

Expected: 2 files staged (`src/main.cpp` new, `CMakeLists.txt` modified).

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(cli): add main with run/list subcommands

Implements `ferret run <bench>` and `ferret list`. Parses --freq with
finite-value validation, applies CLI axis overrides via cli_axis::parse,
expands the sweep cross-product, and drives the runner across every
param point. Buffers every MeasurementRow in memory and only writes
the CSV once the full sweep succeeds — so on any benchmark or sweep
exception the output file/stdout stays empty and exit code is 2 with
a `ferret: ...` message to stderr.

Pre-flights --reps >= 1 and --warmup >= 0 (friendly CLI message),
rejects non-finite ticks_per_ns calibration, and rejects per-row
params that yield zero work (iterations == 0 or sites_per_kernel == 0).
Translates sweep::expand's empty-axis throw into a config-error path.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 5: Verify build (binary now exists)**

```bash
cmake --build build --parallel
ctest --output-on-failure --test-dir build
ls -la build/ferret
```

Expected: `build/ferret` exists. All earlier tests still pass.

---

### Task 12: Replay step 17 — dependent_chain_throughput benchmark

**Files:**
- Create: `benchmarks/dependent_chain_throughput.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Stage source from final state (exact op count via full_blocks + tail)**

```bash
git checkout ee2f556 -- benchmarks/dependent_chain_throughput.cpp
```

- [ ] **Step 2: Stage CMakeLists from 7a2947e**

```bash
git checkout 7a2947e -- CMakeLists.txt
```

- [ ] **Step 3: Verify staging**

```bash
git status
```

- [ ] **Step 4: Commit**

```bash
git commit -m "$(cat <<'EOF'
feat(bench): add dependent_chain_throughput frequency probe

Emits a long single-dependency chain of register-to-register ADDs so
each op waits for the previous. ns_per_site_min therefore approximates
1/frequency on the target CPU. Inner block is 1024 unrolled ADDs;
emit_kernel lays down full_blocks = chain_length / 1024 iterations of
the unrolled block plus a straight-line tail of chain_length % 1024
ADDs so the total op count equals exactly chain_length, including
chain_length < 1024.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 5: Verify build + smoke run**

```bash
cmake --build build --parallel
ctest --output-on-failure --test-dir build
./build/ferret list
```

Expected: `dependent_chain_throughput` appears in `ferret list` output.

---

### Task 13: Replay step 18 — nop padding helper (unchanged)

**Files:**
- Create: `include/ferret/padding.hpp`, `src/padding.cpp`, `tests/test_padding.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

- [ ] **Step 1: Stage all files from original commit**

```bash
git checkout 9607b27 -- include/ferret/padding.hpp src/padding.cpp tests/test_padding.cpp CMakeLists.txt tests/CMakeLists.txt
```

- [ ] **Step 2: Commit and verify**

```bash
git status
git commit -C 9607b27
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

Note: `test_padding` is skipped on aarch64; that's expected.

---

### Task 14: Replay step 19 — direct_branch_footprint benchmark (unchanged)

**Files:**
- Create: `benchmarks/direct_branch_footprint.cpp`
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Stage all files from original commit**

```bash
git checkout bec21ab -- benchmarks/direct_branch_footprint.cpp CMakeLists.txt
```

- [ ] **Step 2: Commit and verify**

```bash
git status
git commit -C bec21ab
cmake --build build --parallel
ctest --output-on-failure --test-dir build
./build/ferret list
```

Expected: both `direct_branch_footprint` and `dependent_chain_throughput` in `ferret list` output.

---

### Task 15: Replay step 20 — scripts (unchanged)

**Files:**
- Create: `scripts/freq.py`, `scripts/plot.py`

- [ ] **Step 1: Stage files from original commit**

```bash
git checkout 18c1567 -- scripts/freq.py scripts/plot.py
```

- [ ] **Step 2: Commit (no test run needed; pure scripts)**

```bash
git status
git commit -C 18c1567
```

---

### Task 16: Replay step 21 — README (unchanged)

**Files:**
- Create: `README.md`

- [ ] **Step 1: Stage README from original commit**

```bash
git checkout 6c1226b -- README.md
```

- [ ] **Step 2: Commit**

```bash
git status
git commit -C 6c1226b
```

---

### Task 17: Replay step 22 — CI workflow (unchanged)

**Files:**
- Create: `.github/workflows/ci.yml`

- [ ] **Step 1: Stage workflow from original commit**

```bash
git checkout 3845da5 -- .github/workflows/ci.yml
```

- [ ] **Step 2: Commit**

```bash
git status
git commit -C 3845da5
```

---

### Task 18: Replay step 23 — integration tests with CTest timeout

**Files:**
- Create: `tests/test_integration.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Stage both files from final state (TIMEOUT 30 baked in)**

```bash
git checkout ee2f556 -- tests/test_integration.cpp tests/CMakeLists.txt
```

- [ ] **Step 2: Verify staging**

```bash
git status
```

Expected: 2 files staged. No top-level `CMakeLists.txt` change here.

- [ ] **Step 3: Commit**

```bash
git commit -m "$(cat <<'EOF'
test: add end-to-end integration tests with CTest timeout

Spawns the ferret binary via std::system and asserts the contract for
both happy paths and every configuration-error path: invalid --freq,
out-of-range --reps/--warmup, log2-axis zero/negative values via range
and single/list, huge --branches values that overflow inside
emit_kernel, mixed sweeps where a later row fails after the first
succeeds, zero-work params, negative values for unsigned params,
non-finite calibration, and empty sweep axes. Asserts exit 2 with a
`ferret: ...` stderr message and a 0-byte output file/stdout for
every error case.

gtest_discover_tests sets PROPERTIES TIMEOUT 30 on test_integration
so a regression that re-introduces a hang fails as a CTest timeout
rather than depending on a coreutils `timeout` binary in PATH.

Co-Authored-By: Claude Opus 4.7 (1M context) <noreply@anthropic.com>
EOF
)"
```

- [ ] **Step 4: Verify build + full test suite**

```bash
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

Expected: full test count matches `ee2f556`'s suite (69 tests pass + 1 skipped on aarch64 host). This is the final test-suite verification before the docs/chore commits.

---

### Task 19: Replay step 24 — gitignore (unchanged)

**Files:**
- Modify: `.gitignore`

- [ ] **Step 1: Stage .gitignore from final state**

```bash
git checkout ee2f556 -- .gitignore
```

- [ ] **Step 2: Commit**

```bash
git status
git commit -C ee2f556
```

---

### Task 20: Cherry-pick the spec-doc commit back onto the rewritten history

**Files:**
- Create: `docs/superpowers/specs/2026-05-11-history-rewrite-design.md` (via cherry-pick)

- [ ] **Step 1: Cherry-pick the spec commit from the backup branch**

```bash
git cherry-pick ccfecda
```

Expected: clean cherry-pick (file didn't exist on current HEAD; no conflicts).

- [ ] **Step 2: Verify the spec doc is at HEAD**

```bash
git log -2 --format="%H %s"
ls docs/superpowers/specs/
```

Expected: spec doc is the tip commit; the previous commit is the chore/gitignore commit.

- [ ] **Step 3: Final verification — working tree must match backup-v1-history exactly**

```bash
git diff backup-v1-history -- .
```

Expected: empty output. If anything appears, halt and inspect — this is the load-bearing invariant of the rewrite.

- [ ] **Step 4: Final clean rebuild + full test run from scratch**

```bash
rm -rf build
cmake -S . -B build
cmake --build build --parallel
ctest --output-on-failure --test-dir build
```

Expected: clean rebuild succeeds; full test suite passes (matches the test count at `backup-v1-history` HEAD).

- [ ] **Step 5: Show new log**

```bash
git log --oneline
echo "---"
git log --oneline backup-v1-history | head -5
```

Expected: new main has ~25 commits (7 unchanged base + 17 rebuilt + 1 spec tip). Stop here and let the user review.

---

### Task 21: Push the rewritten history (user-gated)

**Files:** none — git remote operations only.

- [ ] **Step 1: Confirm user approval to push**

The previous task ends with a stop point so the user can review `git log --oneline`. Do NOT proceed to the push without explicit "go ahead" from the user.

- [ ] **Step 2: Verify backup branch is still safe locally and on origin**

```bash
git log -1 --format="%H %s" backup-v1-history
git ls-remote origin backup-v1-history
```

Expected: both point at `ccfecda docs(spec): add v1 history rewrite design`.

- [ ] **Step 3: Force-push main with lease**

```bash
git push --force-with-lease origin main
```

Expected: push succeeds. `--force-with-lease` rejects the push if `origin/main` has moved since the last fetch, which is a small safety net.

- [ ] **Step 4: Final verification**

```bash
git fetch origin
git status
git log -1 --format="%H %s" origin/main
```

Expected: `main` is up to date with `origin/main`, both at the spec-doc tip commit.

---

## Recovery procedure (if anything goes wrong mid-rewrite)

At any point during tasks 2-19, if a build fails, a test fails, or `git status` shows an unexpected state, recovery is:

```bash
git reset --hard backup-v1-history
```

This puts `main` back to its pre-rewrite state. The backup branch and the remote backup are also intact. Then diagnose the mismatch (likely a missed file in a checkout step), correct the plan, and start the rewrite over from Task 1 Step 1 (which is a no-op if the backup already exists).

If recovery happens after Task 20 Step 1 (cherry-pick already done) but before pushing, the same `git reset --hard backup-v1-history` reverts everything; no force-push has happened yet, so origin is untouched.

If recovery is needed after pushing (Task 21), restore origin from the backup branch:

```bash
git push --force-with-lease origin backup-v1-history:main
```

---

## Self-Review notes (post-write)

- Every source file in the working tree at `ee2f556` is accounted for in a task. (Audited: `include/ferret/*.hpp` × 10, `src/**/*.cpp` × 13, `benchmarks/*.cpp` × 2, `tests/*.cpp` × 13, `scripts/*.py` × 2, `README.md`, `.github/workflows/ci.yml`, `.gitignore`, plus 2 CMakeLists.)
- Each task's commit either includes its own CMakeLists update or doesn't touch any (scripts/readme/CI/gitignore/spec-doc).
- The `expand_log2_range` helper is introduced in Task 3 (axis.hpp) before Task 5 consumes it (cli_axis.cpp) — build order preserved.
- `include/ferret/runner.hpp` is introduced in Task 9 (CSV output, original placement) before Task 10 introduces `src/runner.cpp` — build order preserved.
- `src/main.cpp` (Task 11) depends on every subsystem 8-15 and lands after them.
- Integration tests (Task 18) land after `src/main.cpp` and both benchmark sources, so the `ferret` binary they spawn exists.
- The spec-doc cherry-pick (Task 20) is the only non-mechanical step; the spec file didn't exist on `36b7882` so the pick can't conflict.

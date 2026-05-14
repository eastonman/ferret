# Doc Restructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Split the README along audience lines, give every benchmark a dedicated page with an ASCII kernel-structure diagram, and extract build/CLI/contributing into separate docs.

**Architecture:** Pure documentation refactor. No code under `src/`, `benchmarks/`, `include/`, or `tests/` is touched. New top-level docs live at `docs/build.md`, `docs/cli.md`, `docs/contributing.md`. Per-benchmark pages live under `docs/benchmarks/`. README becomes a slim user-facing landing page.

**Tech Stack:** Markdown only. ASCII diagrams in fenced code blocks. Cross-referenced via relative links.

**Spec:** `docs/superpowers/specs/2026-05-14-doc-restructure-design.md`

**Verification model:** This is a docs-only refactor, so "test then implement" maps to "define the verification check, then create the file, then run the check, then commit." Verification commands use `grep`, `test -f`, and a final link-checker pass.

---

## Task 1: `docs/build.md` — extracted build doc

**Files:**
- Create: `docs/build.md`

- [ ] **Step 1: Define the verification check**

After creation the file must:
- Exist at `docs/build.md`.
- Contain the four expected headings (deps, Nix, plain CMake, sanitizers).

Check: `test -f docs/build.md && grep -c '^## ' docs/build.md` — expect ≥ 4.

- [ ] **Step 2: Create the file**

Content (use exactly this):

````markdown
# Building Ferret

## Dependency requirements

- C++20 compiler
- CMake > 3.20
- Ninja or GNU Make
- Python 3
- Git (for FetchContent and submodules)

## Nix (recommended)

```sh
nix develop
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Plain CMake

Make sure the dependency requirements above are installed.
CMake will FetchContent CLI11, GoogleTest, and sljit if they aren't on
the system search path:

```sh
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Sanitizer builds

Off by default — timing-sensitive runs use clean code. Enable via
`-DFERRET_SANITIZER=<mode>`:

| Mode                | Catches                                                      |
|---------------------|--------------------------------------------------------------|
| `address`           | use-after-free, heap/stack overflow, leaks (LSan, Linux)     |
| `undefined`         | signed overflow, null deref, alignment, type mismatches      |
| `address+undefined` | both of the above (default in CI)                            |
| `thread`            | data races, deadlocks                                        |

```sh
cmake -S . -B build-asan -GNinja -DFERRET_SANITIZER=address+undefined
cmake --build build-asan
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 \
  ctest --test-dir build-asan --output-on-failure
```

CI runs `address+undefined` and `thread` on Linux x86_64 and arm64
(`.github/workflows/sanitizers.yml`). macOS sanitizer support depends
on the toolchain; nixpkgs-clang ASan/TSan currently has runtime-init
issues on Apple Silicon — use UBSan there or build under a Linux
container.
````

- [ ] **Step 3: Run the verification check**

```sh
test -f docs/build.md && grep -c '^## ' docs/build.md
```
Expected: file exists; heading count is 4.

- [ ] **Step 4: Commit**

```sh
git add docs/build.md
git commit -m "docs: extract build instructions into docs/build.md"
```

---

## Task 2: `docs/cli.md` — extracted CLI reference

**Files:**
- Create: `docs/cli.md`

- [ ] **Step 1: Define the verification check**

Check: `test -f docs/cli.md && grep -q 'ferret run' docs/cli.md && grep -q 'Axis syntax' docs/cli.md` — exit 0.

- [ ] **Step 2: Create the file**

Content (use exactly this):

````markdown
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
````

- [ ] **Step 3: Run the verification check**

```sh
test -f docs/cli.md && grep -q 'ferret run' docs/cli.md && grep -q 'Axis syntax' docs/cli.md
```
Expected: exit 0.

- [ ] **Step 4: Commit**

```sh
git add docs/cli.md
git commit -m "docs: extract CLI reference into docs/cli.md"
```

---

## Task 3: `docs/contributing.md` — formatting & linting

**Files:**
- Create: `docs/contributing.md`

- [ ] **Step 1: Define the verification check**

Check: `test -f docs/contributing.md && grep -q 'clang-format' docs/contributing.md && grep -q 'ruff' docs/contributing.md` — exit 0.

- [ ] **Step 2: Create the file**

Content (use exactly this):

````markdown
# Contributing

## Formatting and linting

Formatters and linters run in CI and must pass before merging.

- C++: `clang-format` (style in `.clang-format`) and `clang-tidy`
  (checks in `.clang-tidy`).
- Python: `ruff format` and `ruff check` (config in `pyproject.toml`).

Apply formatters locally:

```sh
./scripts/format.sh
```

Verify the way CI does:

```sh
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
./scripts/lint.sh
```

All tools are provided by `nix develop`.

## Adding a benchmark

See [`writing-a-benchmark.md`](writing-a-benchmark.md) for the
`Benchmark` vtable, the registration macro, and worked examples
covering the frequency-probe and parameter-sweep patterns.
````

- [ ] **Step 3: Run the verification check**

```sh
test -f docs/contributing.md && grep -q 'clang-format' docs/contributing.md && grep -q 'ruff' docs/contributing.md
```
Expected: exit 0.

- [ ] **Step 4: Commit**

```sh
git add docs/contributing.md
git commit -m "docs: extract formatting/linting into docs/contributing.md"
```

---

## Task 4: `docs/benchmarks/dependent_chain_throughput.md` — new per-benchmark page

**Files:**
- Create: `docs/benchmarks/dependent_chain_throughput.md`
- Reference (for accuracy cross-check): `benchmarks/dependent_chain_throughput.cpp`

- [ ] **Step 1: Define the verification check**

The page must:
- Exist.
- Have an Intro, Kernel structure (with ASCII), CLI surface, Reading the curves, Caveats, Related docs sections.
- Mention `chain_length` (the single axis) and `UNROLL = 1024` (the emission detail readers may notice in source).

Check: `test -f docs/benchmarks/dependent_chain_throughput.md && grep -q 'chain_length' docs/benchmarks/dependent_chain_throughput.md && grep -q 'UNROLL' docs/benchmarks/dependent_chain_throughput.md`.

- [ ] **Step 2: Create the file**

Content (use exactly this — note: the ASCII matches `emit_kernel` at `benchmarks/dependent_chain_throughput.cpp:28-58`, which emits a 1024-ADD unrolled inner loop driven by `R1` plus a straight-line tail; all ADDs target `R0`):

````markdown
# `dependent_chain_throughput` — frequency-probe baseline

A back-to-back dependent ADD chain. Every ADD reads and writes the
same register, so it serializes at one ADD latency per op — exactly
one cycle on every common high-perf out-of-order core and on
in-order ARM Cortex-A class cores.

This benchmark exists to **probe the running core frequency** before
running another benchmark on the same core. The runner reports
ns/op; divide cycle count by 1 ns to get GHz. See the project README
for the two-step cycle workflow.

## Kernel structure

```
 (chain_main)
  MOV R0, 1
  MOV R1, full_blocks        ─── outer-loop counter
  loop_top:
    ┌──────────────┐
    │ ADD R0,R0,1  │  ┐
    │ ADD R0,R0,1  │  │
    │ ADD R0,R0,1  │  │  UNROLL = 1024 dependent ADDs
    │   ...        │  │  (RAW on R0 between every pair)
    │ ADD R0,R0,1  │  ┘
    └──────────────┘
    SUB R1,R1,1    ─── full_blocks-times back-edge
    JNZ loop_top
  ── straight-line tail ──
    ADD R0,R0,1    ┐
    ADD R0,R0,1    │  chain_length % UNROLL ADDs
       ...         │
    ADD R0,R0,1    ┘
  RET
```

Annotated:

- All ADDs target `R0` — single live register, RAW dependency between
  every pair, so the chain runs at ADD latency.
- The inner loop body is **`UNROLL = 1024` ADDs** (an
  `emit_kernel`-level constant in `benchmarks/dependent_chain_throughput.cpp`).
  The outer loop runs `chain_length / UNROLL` times.
- A straight-line tail of `chain_length % UNROLL` ADDs makes the total
  op count match `chain_length` exactly. For `chain_length < UNROLL`
  only the tail runs.
- `sites_per_kernel = chain_length` and `iterations = 1`, so the
  runner reports ns per ADD = ns per cycle on a 1-IPC core.

## CLI surface

| flag                    | meaning                                                                 |
| ----------------------- | ----------------------------------------------------------------------- |
| `--chain_length=N`      | Total ADD count emitted into the kernel. Default `100_000_000`.         |

See [`../cli.md`](../cli.md) for global flags (`--core`, `--freq`,
`--reps`, `--warmup`, `--out`, etc.).

The benchmark has no per-bench options.

## Reading the curves

Single-row output: one ns/op number. Divide 1 ns by that number to
get the running frequency in GHz. `scripts/freq.py` does this for
you:

```sh
build/ferret run dependent_chain_throughput --core=3 --out=/tmp/freq.csv
python3 scripts/freq.py /tmp/freq.csv
# → estimated_freq=4.521GHz
```

The reported frequency holds for the duration of the run on the
pinned core, modulo the caveats in the project README's discipline
section (frequency scaling, heterogeneous cores, Apple Silicon
pinning).

## Caveats

- **1-IPC assumption.** The whole probe rests on dependent ADD
  latency = 1 cycle. This holds on every shipping x86_64 and arm64
  core ferret targets. If you port ferret to an unusual architecture,
  validate this assumption first.
- **Single-threaded.** The probe pins to one core; the reported
  frequency is whatever that core was running at during the
  measurement, which may differ from other cores on heterogeneous
  CPUs.

## Related docs

- Construction rationale: [`writing-a-benchmark.md`](../writing-a-benchmark.md) (worked example A).
- Project two-step workflow: [project README](../../README.md).
````

- [ ] **Step 3: Run the verification check**

```sh
test -f docs/benchmarks/dependent_chain_throughput.md && grep -q 'chain_length' docs/benchmarks/dependent_chain_throughput.md && grep -q 'UNROLL' docs/benchmarks/dependent_chain_throughput.md
```
Expected: exit 0.

- [ ] **Step 4: Commit**

```sh
git add docs/benchmarks/dependent_chain_throughput.md
git commit -m "docs: add per-benchmark page for dependent_chain_throughput"
```

---

## Task 5: `docs/benchmarks/direct_branch_footprint.md` — new per-benchmark page

**Files:**
- Create: `docs/benchmarks/direct_branch_footprint.md`
- Reference (for accuracy cross-check): `benchmarks/direct_branch_footprint.cpp`

- [ ] **Step 1: Define the verification check**

Check: `test -f docs/benchmarks/direct_branch_footprint.md && grep -q 'sattolo_permute' docs/benchmarks/direct_branch_footprint.md && grep -q 'spacing_bytes' docs/benchmarks/direct_branch_footprint.md`.

- [ ] **Step 2: Create the file**

Content (use exactly this — ASCII derived from `emit_kernel` at `benchmarks/direct_branch_footprint.cpp:91-178`, including the `kJumpBytes` arch-specific encoding and the `labels[branches]` exit label):

````markdown
# `direct_branch_footprint` — direct-jump BTB capacity

`N` unconditional direct branches at PC = base + i × spacing,
chained so exactly `N` branches execute per outer-loop iteration.
The outer loop amortizes the runner's tick-read overhead.

The per-site cost stays flat while `N` ≤ direct-BTB capacity; once
`N` exceeds it, mispredicts compound and per-branch cost steps up.
The cliff position is the direct-jump BTB capacity.

## Kernel structure

```
   PC                  site
 0x0000   ┌───────────────────────────┐
          │  B   target_0             │ ──┐
          │  <NOP pad to spacing>     │   │
 base+1×spacing   ┌───────────────────┐   │  spacing_bytes (default 64)
          │  B   target_1             │ ──┼─┐
          │  <NOP pad to spacing>     │   │ │
 base+2×spacing   ┌───────────────────┐   │ │
          │  B   target_2             │ ──┼─┼─┐
          │   ...                     │   │ │ │
          ├───────────────────────────┤   │ │ │
          │  exit_label  ──── SUB,JNZ │   │ │ │
          │              ── loop_top  │   │ │ │
          └───────────────────────────┘   │ │ │
                                          │ │ │
   sattolo_permute=0 (default):           │ │ │
     target_i = site_{i+1}    ◄───────────┘─┘─┘   sequential fall-through
   sattolo_permute=1:
     target_i = π(i),  π = Sattolo cycle         single Hamiltonian cycle
                                                 (breaks I-cache spatial prefetch)
```

Annotated:

- Each site is `kJumpBytes` of branch encoding plus NOP padding to
  `spacing`: 4 + (spacing − 4) on AArch64; 5 + (spacing − 5) on
  x86_64. Layout is verified post-codegen by `verify_layout()`.
- The exit label (`labels[branches]` in the source) is the
  post-chain return point: the chain falls through there exactly
  once per iteration, the outer loop decrements and branches back to
  `loop_top`.
- With `sattolo_permute=1` the unique cycle edge pointing back to
  `labels[0]` is rerouted to the exit label so each iteration still
  executes exactly `N` branches (Sattolo's algorithm; see
  `include/ferret/permute.hpp`).

## Per-benchmark options

`--sattolo_permute=0|1` (default `0`).

- `0`: wires each branch to fall through to the next in layout order.
  Sequential PC walk — measures BTB *plus* whatever sequential
  prefetch the front-end does.
- `1`: rewires the jump targets as a uniform random Hamiltonian
  cycle over the same `N` branches (Sattolo's algorithm, seeded by
  `--seed` mixed with `branches` and `spacing_bytes`). `N` branches
  still execute per iteration but the executed PC order is
  unpredictable — useful for isolating the BTB contribution from
  sequential-prefetch and I-cache spatial-locality effects.

## CLI surface

| flag                       | meaning                                                                |
| -------------------------- | ---------------------------------------------------------------------- |
| `--branches=A..B`          | Log₂ sweep, e.g. `1..32768`. Site count.                               |
| `--branches=v1,v2,…`       | Explicit list.                                                         |
| `--spacing_bytes=A..B`     | Log₂ sweep over `{16, 32, 64, 128}`. Site stride.                      |
| `--sattolo_permute=0\|1`   | See above. Default `0`.                                                |
| `--seed=…`                 | Seeds the Sattolo cycle (mixed with branches/spacing).                 |

See [`../cli.md`](../cli.md) for global flags.

## Reading the curves

Plot `cycles_per_site` vs. `branches` on a log-x axis. Below the BTB
capacity the curve is flat near the front-end's branch-per-cycle
limit. Past the capacity, per-branch cost climbs as more sites miss
in the BTB.

`sattolo_permute=1` typically lowers the apparent capacity (because
the predictor can no longer ride the sequential pattern) and
sharpens the cliff.

## Caveats

- **Outer-loop pollution.** The decrement + back-edge at the exit
  label add one indirect branch worth of overhead per iteration.
  Divided across `N` sites it's negligible for `N ≥ 64`, but
  smaller `N` curves carry a measurable per-iteration tax.
- **`spacing_bytes < kJumpBytes` rejected.** On x86_64 each branch
  is 5 B; `spacing_bytes` ≥ 5 (must also be `kBranchAlign`-aligned,
  which is 1 on x86_64 and 4 on AArch64).
- **Apple Silicon pinning.** See the project README's discipline
  section — probe and benchmark land on *some* P-core, not
  necessarily the same one.

## Related docs

- Construction rationale: [`writing-a-benchmark.md`](../writing-a-benchmark.md) (worked example B).
- Project two-step workflow: [project README](../../README.md).
````

- [ ] **Step 3: Run the verification check**

```sh
test -f docs/benchmarks/direct_branch_footprint.md && grep -q 'sattolo_permute' docs/benchmarks/direct_branch_footprint.md && grep -q 'spacing_bytes' docs/benchmarks/direct_branch_footprint.md
```
Expected: exit 0.

- [ ] **Step 4: Commit**

```sh
git add docs/benchmarks/direct_branch_footprint.md
git commit -m "docs: add per-benchmark page for direct_branch_footprint"
```

---

## Task 6: Update `docs/benchmarks/nested_call_depth.md`

**Files:**
- Modify: `docs/benchmarks/nested_call_depth.md`

**What changes:**
1. Add a **Kernel structure** section with the ASCII diagram (the only currently missing template section).
2. Remove the existing **The two-step workflow** section (per template change — workflow lives in README).
3. Replace its CLI-surface preamble pointer with a link to `../cli.md` for global flags.

- [ ] **Step 1: Define the verification check**

After modification:
- Section "## Kernel structure" exists.
- Section "## The two-step workflow" does **not** exist.
- A `../cli.md` reference exists.
- All three variants (0/1/2) still appear under their existing variant headings.

Check:
```sh
grep -q '^## Kernel structure' docs/benchmarks/nested_call_depth.md && \
  ! grep -q '^## The two-step workflow' docs/benchmarks/nested_call_depth.md && \
  grep -q '\.\./cli\.md' docs/benchmarks/nested_call_depth.md && \
  grep -q '^### \`--variant=0\`' docs/benchmarks/nested_call_depth.md && \
  grep -q '^### \`--variant=1\`' docs/benchmarks/nested_call_depth.md && \
  grep -q '^### \`--variant=2\`' docs/benchmarks/nested_call_depth.md
```
Expected: exit 0.

- [ ] **Step 2: Insert "Kernel structure" section**

Insert this section **immediately after** the existing intro paragraphs (the paragraphs starting "Probes the depth…" and "Per-call cost…") and **before** the existing `## The three kernel variants` heading.

ASCII derived from `emit_variant1_counter_bit` at `benchmarks/nested_call_depth.cpp:105-159` — the dispatch is `AND S0, 1; JZ → site_b; site_a: CALL+JMP done; site_b: CALL; done: RET`. Variant 0 uses one CALL site per body; variant 2 uses `emit_k8_dispatch` (a 3-CB binary tree over a path-table byte) for 8 sites per body.

````markdown
## Kernel structure

Variant 1 (default) is the canonical picture; variants 0 and 2
differ in fan-out per body. All three emit `depth + 1` functions
(`chain_main` + `BODY_1`…`BODY_depth`), with `chain_main` running
the outer loop and calling `BODY_depth`. Each `BODY_d` calls
`BODY_{d-1}` through one or more dispatch-selected sites. `BODY_0`
is a bare RET.

```
 chain_main:                    BODY_d  (variant 1, 1 ≤ d ≤ depth):
 ┌─────────────────────────┐    ┌──────────────────────────┐
 │ MOV  S0, iters          │    │ AND  S0, 1               │
 │ loop_top:               │    │ JZ   site_b              │
 │   CALL BODY_depth       │    │ site_a: CALL BODY_{d-1}  │ ──┐
 │   SUB  S0,S0,1; JNZ ↩  │    │ JMP  done                │   │
 │ RET                     │    │ site_b: CALL BODY_{d-1}  │ ──┤
 └─────────────────────────┘    │ done: RET                │   │
                                └──────────────────────────┘   │
   S0 = iteration counter,                                     │
   threaded through the chain                                  │
   in a callee-saved register                              ◀───┘
                                                          (next deeper body
                                                           returns through here)
 BODY_0:
 ┌─────────────────────────┐
 │ RET                     │  ← deepest level; pops the last return addr
 └─────────────────────────┘
```

Per-body fan-out is what `--variant` selects:

- **`--variant=0`** — one CALL site per body, no dispatch. Direct
  RET, single target per RET PC.
- **`--variant=1`** — two CALL sites per body, one CB dispatching
  on bit 0 of `S0` (perfectly predicted alternation).
- **`--variant=2`** — eight CALL sites per body, three CBs forming
  a binary tree dispatching on bits of a byte loaded from
  `path_table[row][i]`, where `row = S0 & (path_table_rows − 1)`
  rotates per outer iteration.
````

- [ ] **Step 3: Delete the "The two-step workflow" section**

Remove the entire section from `## The two-step workflow` through (and including) the paragraph that ends with "Omitting `--variant` runs just the default (variant 1)."

The CLI-surface section (`## CLI surface`) follows immediately after.

- [ ] **Step 4: Retarget global-flag references**

In the `## CLI surface` table, the rows marked "Standard ferret flag" (`--freq=…`, `--core=…`, `--reps=K`, `--warmup=W`, `--seed=…`) currently say "Standard." in the meaning column. Add a sentence to the section preamble (just before the table) pointing readers to `../cli.md`:

```markdown
Global flags (`--core`, `--freq`, `--reps`, `--warmup`, `--out`,
`--log-level`, `--seed`) are documented in [`../cli.md`](../cli.md);
the table below lists only the axes and options specific to this
benchmark.
```

Then trim the "Standard ferret flag" rows from the table, leaving only `--depth`, `--variant`, and `--path_table_rows`. (Keep the per-row explanation for `--seed` if any benchmark-specific behavior exists — for `nested_call_depth`, the seed seeds the variant-2 path-table PRNG, which **is** benchmark-specific, so retain its row.)

Final table (use exactly this):

```markdown
| flag                  | meaning                                                  |
| --------------------- | -------------------------------------------------------- |
| `--depth=A..B`        | Sweep nesting depth from A to B inclusive, step 1.       |
| `--depth=v1,v2,…`     | Sweep an explicit list of depths.                        |
| `--variant=0\|1\|2`   | Kernel construction (default 1). See the variants section above. |
| `--path_table_rows=N` | Used **only by variant 2**. Per-iteration dispatch table row count, default 256, must be a power of two ≥ 2. Bigger ⇒ longer dispatch-pattern period (harder for indirect predictors to learn) but bigger memory footprint (`N × (depth+1)` bytes); once the table exceeds L1D, per-call cost inflates progressively with depth and the pre-cliff curve becomes a measurement of cache pressure rather than RAS pressure. The default keeps the table at ≤ 16 KB through depth 64 so it stays in L1D on every shipping core. |
| `--seed=…`            | Seeds the variant-2 path-table PRNG; combined with `depth` via `std::seed_seq` so distinct sweep points get distinct dispatch streams. Ignored by variants 0 and 1. |
```

- [ ] **Step 5: Run the verification check**

```sh
grep -q '^## Kernel structure' docs/benchmarks/nested_call_depth.md && \
  ! grep -q '^## The two-step workflow' docs/benchmarks/nested_call_depth.md && \
  grep -q '\.\./cli\.md' docs/benchmarks/nested_call_depth.md && \
  grep -q '^### `--variant=0`' docs/benchmarks/nested_call_depth.md && \
  grep -q '^### `--variant=1`' docs/benchmarks/nested_call_depth.md && \
  grep -q '^### `--variant=2`' docs/benchmarks/nested_call_depth.md
echo "exit=$?"
```
Expected: `exit=0`.

- [ ] **Step 6: Commit**

```sh
git add docs/benchmarks/nested_call_depth.md
git commit -m "docs(nested_call_depth): add kernel-structure ASCII, retarget to docs/cli.md"
```

---

## Task 7: Slim `README.md`

**Files:**
- Modify: `README.md`

**What changes:** Replace the README in-place with the slim landing-page version. Removed content has been migrated to Tasks 1–3 already.

- [ ] **Step 1: Define the verification check**

After rewrite the README must:
- Still contain "Two-step cycle workflow" (kept verbatim).
- Still contain "Discipline" (kept verbatim).
- **Not** contain "## Build" with the full sub-recipes, "## CLI flags", "## Formatting and linting", or the per-benchmark per-option blurbs.
- Link to all five new docs (`docs/build.md`, `docs/cli.md`, `docs/contributing.md`, and the two new benchmark pages) plus the existing two (`docs/architecture.md`, `docs/writing-a-benchmark.md`).

Check:
```sh
grep -q 'two-step cycle' README.md && \
  grep -q '^## Discipline' README.md && \
  ! grep -q '^## Formatting and linting' README.md && \
  ! grep -q '^## CLI flags' README.md && \
  grep -q 'docs/build.md' README.md && \
  grep -q 'docs/cli.md' README.md && \
  grep -q 'docs/contributing.md' README.md && \
  grep -q 'docs/benchmarks/dependent_chain_throughput.md' README.md && \
  grep -q 'docs/benchmarks/direct_branch_footprint.md' README.md && \
  grep -q 'docs/benchmarks/nested_call_depth.md' README.md
echo "exit=$?"
```
Expected: `exit=0`.

- [ ] **Step 2: Rewrite README in place**

Replace the entire file with this content:

````markdown
# Ferret

**F**ront**E**nd **R**eve**R**se-**E**ngineering **T**oolkit — a JIT-driven cross-platform
microbenchmark framework for probing CPU frontend microarchitectural
structures (BTB, RAS, BPU, decoded-uop cache, ITLB, …).

Ferret emits parameterized microbenchmarks at runtime, measures their
per-site cost via free-running timing counters, sweeps a parameter axis,
and writes one CSV row per parameter point. A Python script plots the
resulting curves so you can spot capacity/associativity cliffs.

## Supported platforms

| Arch    | Linux | macOS | Android |
| ------- | :---: | :---: | :-----: |
| x86_64  |   ✓   |   —   |    ✓    |
| AArch64 |   ✓   |   ✓   |    ✓    |

RISC-V and LoongArch are reachable through sljit but not yet supported.

## Quickstart

```sh
nix develop
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

Full build options, sanitizer matrix, and non-Nix recipes: [`docs/build.md`](docs/build.md).

## The two-step cycle workflow

Ferret reports per-site cost in **CPU cycles** when you supply the
running core frequency, in **nanoseconds** otherwise. Cycles are the
preferred unit because the absolute number is meaningful information
about the structure under test. Ferret never auto-probes the
frequency — it asks you to do it explicitly.

```sh
# Step 1: probe the running frequency on core 3.
build/ferret run dependent_chain_throughput --core=3 --out=/tmp/freq.csv
python3 scripts/freq.py /tmp/freq.csv
# → estimated_freq=4.521GHz

# Step 2: run the actual benchmark with --freq, pinned to the same core.
build/ferret run direct_branch_footprint --core=3 \
    --branches=1..32768 --spacing_bytes=64 \
    --freq=4.521GHz --out=/tmp/btb.csv

# Step 3: plot — picks cycles_per_site automatically because freq was set.
python3 scripts/plot.py /tmp/btb.csv --out=/tmp/btb.png
```

CLI flags and axis syntax: [`docs/cli.md`](docs/cli.md).

## Benchmarks

| Name                                                                             | Targets                                  |
| -------------------------------------------------------------------------------- | ---------------------------------------- |
| [`dependent_chain_throughput`](docs/benchmarks/dependent_chain_throughput.md)    | running core frequency / 1-IPC baseline  |
| [`direct_branch_footprint`](docs/benchmarks/direct_branch_footprint.md)          | direct-jump BTB capacity                 |
| [`nested_call_depth`](docs/benchmarks/nested_call_depth.md)                      | Return Address Stack (RAS) depth         |

Each benchmark page has the kernel structure, CLI surface, and reading-the-curves guide.

```sh
build/ferret list   # registered benchmark names
```

## Discipline (a.k.a. caveats)

Ferret does what user-space can do to make timing reliable: pins a
core, raises priority, mlocks memory, runs warmup iterations, takes
the **min** of K repetitions per data point. Everything else is your
responsibility:

- **Frequency scaling.** Ferret cannot pin core frequency without root.
  Run with a fixed-frequency governor (Linux: `cpupower frequency-set
-g performance`) or document that boost was active.
- **Heterogeneous cores** (Apple P/E, ARM big.LITTLE, Android). Pin
  with `--core=` so probe and target benchmark execute on the _same_
  core. Different cores can have different microarchitectures and
  different running frequencies.
- **Apple Silicon pinning.** macOS on arm64 (M-series) does not
  implement per-core thread affinity — `thread_policy_set` returns
  `KERN_NOT_SUPPORTED` for every core number. On that platform ferret
  falls back to a `QOS_CLASS_USER_INTERACTIVE` hint that strongly
  prefers the P-cluster, prints a warning, and treats `--core=N` as
  informational. Probe and benchmark land on _some_ P-core, not
  necessarily the same one, so cycle counts are stable per-cluster but
  not per-core. Run with `taskpolicy -b` or `sudo nice` if you need
  stronger guarantees.
- **Frequency-probe assumption.** `dependent_chain_throughput` assumes
  dependent ADD latency = 1 cycle. This holds on every common high-perf
  out-of-order core and on in-order ARM Cortex-A class cores. If you
  use ferret on an unusual architecture, validate the assumption first.
- **System noise.** App Nap (macOS), Doze (Android), background tasks,
  AV scanners, etc. Ferret does not detect or report on noise levels —
  it just runs. If your data looks suspicious, quiesce the box and
  rerun.

## Documentation

- [`docs/architecture.md`](docs/architecture.md) — codebase overview and module map.
- [`docs/build.md`](docs/build.md) — full build, sanitizer matrix.
- [`docs/cli.md`](docs/cli.md) — global CLI flags and axis syntax.
- [`docs/benchmarks/`](docs/benchmarks/) — per-benchmark kernel structure and options.
- [`docs/writing-a-benchmark.md`](docs/writing-a-benchmark.md) — guide for adding a new benchmark.
- [`docs/contributing.md`](docs/contributing.md) — formatting and linting.
````

- [ ] **Step 3: Run the verification check**

```sh
grep -q 'two-step cycle' README.md && \
  grep -q '^## Discipline' README.md && \
  ! grep -q '^## Formatting and linting' README.md && \
  ! grep -q '^## CLI flags' README.md && \
  grep -q 'docs/build.md' README.md && \
  grep -q 'docs/cli.md' README.md && \
  grep -q 'docs/contributing.md' README.md && \
  grep -q 'docs/benchmarks/dependent_chain_throughput.md' README.md && \
  grep -q 'docs/benchmarks/direct_branch_footprint.md' README.md && \
  grep -q 'docs/benchmarks/nested_call_depth.md' README.md
echo "exit=$?"
```
Expected: `exit=0`.

- [ ] **Step 4: Commit**

```sh
git add README.md
git commit -m "docs(readme): slim to landing page; extract build, CLI, formatting into docs/"
```

---

## Task 8: Update `docs/README.md` index

**Files:**
- Modify: `docs/README.md`

- [ ] **Step 1: Define the verification check**

After update the index must:
- Link to the three new top-level docs.
- Link to the two new per-benchmark pages plus the existing one.
- Still preserve the "Historical artifacts" disclaimer about `superpowers/`.

Check:
```sh
grep -q 'build.md' docs/README.md && \
  grep -q 'cli.md' docs/README.md && \
  grep -q 'contributing.md' docs/README.md && \
  grep -q 'benchmarks/dependent_chain_throughput' docs/README.md && \
  grep -q 'benchmarks/direct_branch_footprint' docs/README.md && \
  grep -q 'benchmarks/nested_call_depth' docs/README.md && \
  grep -q 'Historical artifacts' docs/README.md
echo "exit=$?"
```
Expected: `exit=0`.

- [ ] **Step 2: Rewrite `docs/README.md` in place**

Replace the entire file with:

````markdown
# Ferret Documentation

## Live documentation

These docs reflect current behavior. Edit them when behavior changes.

- [`architecture.md`](architecture.md) — codebase overview and module map.
- [`build.md`](build.md) — dependency requirements, Nix/CMake recipes, sanitizer matrix.
- [`cli.md`](cli.md) — global `ferret run` flags and axis syntax.
- [`writing-a-benchmark.md`](writing-a-benchmark.md) — guide for adding a new benchmark.
- [`contributing.md`](contributing.md) — formatting and linting.
- [`../README.md`](../README.md) — user-facing pitch, quickstart, two-step cycle workflow, discipline.

### Benchmarks

One page per benchmark, with kernel-structure ASCII, CLI surface, and reading-the-curves notes.

- [`benchmarks/dependent_chain_throughput.md`](benchmarks/dependent_chain_throughput.md) — frequency-probe baseline.
- [`benchmarks/direct_branch_footprint.md`](benchmarks/direct_branch_footprint.md) — direct-jump BTB capacity.
- [`benchmarks/nested_call_depth.md`](benchmarks/nested_call_depth.md) — RAS depth.

## Historical artifacts

The `superpowers/specs/` and `superpowers/plans/` directories hold
point-in-time design and implementation records. They are kept for
git-archaeology context — to see what we were thinking and what we
chose not to do at a given moment.

**They are not current documentation.** If a spec disagrees with the
code, the code is right and the spec is frozen.
````

- [ ] **Step 3: Run the verification check**

```sh
grep -q 'build.md' docs/README.md && \
  grep -q 'cli.md' docs/README.md && \
  grep -q 'contributing.md' docs/README.md && \
  grep -q 'benchmarks/dependent_chain_throughput' docs/README.md && \
  grep -q 'benchmarks/direct_branch_footprint' docs/README.md && \
  grep -q 'benchmarks/nested_call_depth' docs/README.md && \
  grep -q 'Historical artifacts' docs/README.md
echo "exit=$?"
```
Expected: `exit=0`.

- [ ] **Step 4: Commit**

```sh
git add docs/README.md
git commit -m "docs: update docs/README.md index for the new layout"
```

---

## Task 9: Final link integrity sweep

**Files:**
- Read: every `.md` under `docs/` and the top-level `README.md`.

- [ ] **Step 1: Run a relative-link scan**

Extract every Markdown link `(path.md)` or `(path.md#anchor)` from
`README.md` and `docs/**/*.md`, ignore anything under `docs/superpowers/`
(those are point-in-time specs, allowed to reference old paths), then
verify each resolved file exists:

```sh
python3 - <<'PY'
import re, pathlib, sys

root = pathlib.Path('.').resolve()
bad = []
md_files = [pathlib.Path('README.md')] + sorted(pathlib.Path('docs').rglob('*.md'))
md_files = [p for p in md_files if 'superpowers/' not in str(p)]

link_re = re.compile(r'\]\(([^)#]+)(?:#[^)]*)?\)')
for f in md_files:
    text = f.read_text()
    for m in link_re.finditer(text):
        target = m.group(1)
        if target.startswith(('http://', 'https://', 'mailto:')):
            continue
        resolved = (f.parent / target).resolve()
        if not resolved.exists():
            bad.append(f"{f}: -> {target} (resolved {resolved})")

if bad:
    print('BROKEN LINKS:')
    for b in bad:
        print(' ', b)
    sys.exit(1)
print('All relative links resolve.')
PY
```
Expected: `All relative links resolve.` and exit 0.

- [ ] **Step 2: Confirm the doc tree matches the spec target layout**

```sh
ls docs/build.md docs/cli.md docs/contributing.md \
   docs/benchmarks/dependent_chain_throughput.md \
   docs/benchmarks/direct_branch_footprint.md \
   docs/benchmarks/nested_call_depth.md \
   docs/architecture.md docs/writing-a-benchmark.md \
   docs/README.md
```
Expected: every path printed; no errors.

- [ ] **Step 3: Verify no orphaned content fragments**

The README rewrite should have completely replaced the file; check
no leftover "## Build" section (we removed the deep recipe) or full
sanitizer table remains:

```sh
grep -E '^### Sanitizer builds|^## CLI flags|^### Plain CMake|^## Formatting and linting' README.md
```
Expected: no output (exit 1 from `grep`, which is fine — the intent is "find these and complain if they exist").

- [ ] **Step 4: No commit needed**

If a check fails, return to the responsible task and fix the underlying file. Otherwise the refactor is complete.

---

## Verification matrix

Quick sanity sweep mapping spec deliverables to tasks:

| Spec requirement                                          | Task    |
| --------------------------------------------------------- | ------- |
| `docs/build.md` exists with full build + sanitizer recipe | Task 1  |
| `docs/cli.md` exists with global flags + axis syntax      | Task 2  |
| `docs/contributing.md` exists with format/lint            | Task 3  |
| Per-benchmark page for `dependent_chain_throughput`       | Task 4  |
| Per-benchmark page for `direct_branch_footprint`          | Task 5  |
| `nested_call_depth.md` gains ASCII, drops two-step block  | Task 6  |
| README slimmed to landing-page form                       | Task 7  |
| `docs/README.md` index updated                            | Task 8  |
| All cross-links resolve                                   | Task 9  |
| ASCII matches actual `emit_kernel()` for each benchmark   | Task 4 / 5 / 6 (ASCII pre-validated against source while writing this plan) |

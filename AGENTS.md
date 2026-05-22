# AGENTS.md

Operational guide for both human contributors and agentic workers
(LLM coding assistants). The discursive docs live in
[`docs/`](docs/); this file is the short, command-oriented checklist
of what to run, in what order, before opening a PR.

## What ferret is

A JIT-driven microbenchmark framework for probing CPU frontend
microarchitectural structures (BTB, RAS, BPU, decoded-uop cache,
ITLB). C++20 core under `src/`, benchmarks under `benchmarks/`, a
Python plotting CLI under `scripts/`. See [`README.md`](README.md)
and [`docs/architecture.md`](docs/architecture.md) for the full
mental model.

## TL;DR pre-PR sequence

Inside `nix develop` (or with the equivalent tools on `PATH`):

```sh
# format C++ + Python in place.
./scripts/format.sh

# generate compile_commands.json for lint.
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

# build all targets.
cmake --build build

# run C++ tests.
ctest --test-dir build --output-on-failure

# run Python tests (skips the `integration` marker).
./scripts/test_py.sh

# verify everything CI verifies — this is the gate.
./scripts/lint.sh
```

All six must exit zero before opening a PR. There are **no
pre-commit hooks** — `lint.sh` is enforced only in CI, so running
it locally is the only way to catch a CI failure before pushing.

## Build directory rule

**Always use `build/`.** Several `build-*/` directories may exist
locally from past experiments (`build-codereview/`, `build-lint/`,
`build-x86-linux-asanubsan/`, …); they are not canonical and not
tracked by git. `scripts/lint.sh` reads `build/compile_commands.json`.
If you must keep a side-build (e.g., a sanitizer tree), use a
distinct directory and leave `build/` as the lint-friendly tree.

## C++ workflow specifics

- **Warnings are errors.** `FERRET_WERROR=ON` is the CMake default
  and applies to ferret's own targets (not vendored deps). Do not
  commit a build that required `-DFERRET_WERROR=OFF`.
- **clang-tidy is hard.** `.clang-tidy` has `WarningsAsErrors: '*'`,
  so any diagnostic from the configured `bugprone-*`, `performance-*`,
  `readability-*`, `modernize-*` checks fails CI. `lint.sh` skips
  files under `tests/` and under `_deps/`; everything else under
  `src/`, `include/`, `benchmarks/` is linted at the compile-commands
  level.
- **Sanitizer trees are separate.** ASan and TSan need independent
  instrumented builds — configure them into distinct directories
  (`build-asan/`, `build-tsan/`). See
  [`docs/build.md`](docs/build.md) for the env-var recipe that
  matches CI.
- **Per-platform sources.** `CMakeLists.txt` picks
  `src/timing/{x86_64,aarch64}.cpp`, `src/pinning/{linux,macos}.cpp`,
  and `src/padding/{x86_64,aarch64}.cpp` at configure time. If you
  touch one arch/OS path, build the matching one too — CI runs both.

## Python workflow specifics

- `ruff>=0.14` is required (`pyproject.toml` pins `required-version`);
  older ruff hard-errors on startup. The Nix dev shell provides a
  pinned version.
- `tests/python/` is the suite; `conftest.py` puts `scripts/` on
  `sys.path` so `import ferret_plot` works without an install.
- The `integration` marker gates tests that spawn Chrome via kaleido
  (`tests/python/test_integration_export.py`). `scripts/test_py.sh`
  excludes them with `-m "not integration"`; CI runs the integration
  suite separately on Linux + macOS with Chrome on `PATH`.

## Running a single test

```sh
ctest --test-dir build -R test_timing --output-on-failure
./build/tests/test_timing --gtest_filter='Timing.TicksPerNsIsPositive'

python3 -m pytest tests/python/test_surface.py
python3 -m pytest tests/python/test_surface.py::test_some_case -v
python3 -m pytest tests/python -m integration -v   # requires Chrome
```

`ctest` only knows about C++ targets — Python tests are not
registered with it.

## Commit messages

`type(scope): subject` (Conventional-Commits-ish). Scope is
optional; subject is lowercase, imperative, no trailing period.

Types in use: `feat`, `fix`, `refactor`, `style`, `perf`, `build`,
`ci`, `test`, `docs`, `lint`. Examples from `git log`:

```
refactor(surface): decompose make_figure and name tunables
perf(plot): lazy-import kinds and clean up browser staging files
build: tighten sljit visibility and share benchmark objects
fix(plot): export surface png via chromium webgl fallback
lint: silence ruff on intentional lazy imports and wide signatures
```

Forbidden in commit messages, PR titles, PR bodies, and code
comments:

- AI tool names (Codex, Claude, Grok, Gemini, …) — including
  `Co-Authored-By:` footers.
- Process narration ("FIXED", "Step 3", "Week 2", "Phase 1",
  "AC-x"). Write what the change *is*, not how the work
  progressed.

## Branches and PRs

Local branches follow `type/short-description`, matching the commit
type vocabulary: `feat/branch-history-footprint`,
`fix/surface-png-webgl-export`, `refactor/code-review-cleanup`,
`ci/workflow-hardening`.

PRs target `main`. CI must be green:

| Workflow             | What it gates                                                                |
|----------------------|------------------------------------------------------------------------------|
| `lint.yml`           | `cmake -S . -B build … && ./scripts/lint.sh` (clang-format, ruff, clang-tidy) |
| `build.yml`          | Build + ctest across gcc-13/14, clang-17/18 on linux-x86_64; gcc-14/clang-18 on linux-arm64; Apple Clang on macos-arm64 |
| `python.yml`         | `scripts/test_py.sh` + integration tests on linux-nix, linux-pip, macos-pip   |
| `sanitizers.yml`     | `address+undefined` (all OS) + `thread` (Linux only) on Debug builds         |
| `nix.yml`            | `nix flake check`                                                            |
| `codeql.yml`         | CodeQL c-cpp + python; weekly cron                                           |
| `zizmor.yml`         | Workflow YAML security audit; weekly cron                                    |

## Footguns

- **`lint.sh` exits 1 with no useful output if `build/compile_commands.json` is missing.** The configure step is not optional.
- **clang-tidy from non-Nix toolchains may diverge** from the version CI pins. If `./scripts/lint.sh` passes locally but the `lint.yml` job fails, suspect version skew — re-run under `nix develop`.
- **kaleido version split.** The Nix dev shell currently ships kaleido 0.2.1 (nixpkgs-25.11); the pip path uses `>=1.0`. The two have different internal export paths. `test_integration_export.py` exercises both in CI; expect different code paths on each.
- **`pinning` privileged operations skip silently.** `pin_to_core`, `boost_priority` (via `setpriority(-10)`), and `lock_memory` (`mlockall`) need elevated privileges. The corresponding tests skip themselves when `geteuid() != 0`. Benchmark *correctness* is unaffected, but *timing reproducibility* requires running as root or with the right capabilities.
- **`detect_leaks` on macOS aborts startup.** Do not name it in `ASAN_OPTIONS` when running sanitizer tests on macOS — see [`docs/build.md`](docs/build.md).
- **Apple Silicon has no per-core affinity.** `--core=N` is informational on macOS arm64; probe and benchmark land on *some* P-core, not necessarily the same one. See the README's discipline section.
- **`benchmark-results/` is not gitignored** while `*.csv`, `*.html`, and `*.png` are. Take care not to commit large CSV/PNG outputs into other paths.

## Where to find things

| If you need…                              | Read this                                           |
|-------------------------------------------|-----------------------------------------------------|
| Module map of `src/` and `include/ferret/` | [`docs/architecture.md`](docs/architecture.md)      |
| How to add a benchmark                    | [`docs/writing-a-benchmark.md`](docs/writing-a-benchmark.md) |
| Global `ferret run` flags and axis syntax | [`docs/cli.md`](docs/cli.md)                        |
| Full build options + sanitizer matrix     | [`docs/build.md`](docs/build.md)                    |
| Per-benchmark kernel structure            | [`docs/benchmarks/`](docs/benchmarks/)              |
| Frequency probe, benchmark, and plot workflow + caveats | [`README.md`](README.md)                 |

`superpowers/specs/` and `superpowers/plans/` are point-in-time
artifacts. If they disagree with current code, the code is right.

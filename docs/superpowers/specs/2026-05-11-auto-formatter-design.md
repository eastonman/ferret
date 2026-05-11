# Auto-Formatter and Linter Setup — Design Spec

**Date:** 2026-05-11
**Status:** Design — pending implementation plan

## Goal

Configure auto-formatters and linters for both C++ and Python in the ferret
repository, with CI enforcement that fails the build on any formatting or
lint violation.

## Tool selection

| Language | Formatter      | Linter        | Config file                                |
|----------|----------------|---------------|--------------------------------------------|
| C++      | `clang-format` | `clang-tidy`  | `.clang-format`, `.clang-tidy`             |
| Python   | `ruff format`  | `ruff check`  | `pyproject.toml` `[tool.ruff]`, `[tool.ruff.lint]` |

`ruff` is chosen over `black` per user direction: it replaces `black`,
`isort`, `flake8` and parts of `pylint` with one fast tool.

## Style

### C++ formatter (`.clang-format`)

- `BasedOnStyle: Google`
- `ColumnLimit: 120`
- `IndentWidth: 2`
- `PointerAlignment: Left`
- `DerivePointerAlignment: false`

### C++ linter (`.clang-tidy`)

- `Checks: 'bugprone-*, performance-*, readability-*, modernize-*, -modernize-use-trailing-return-type'`
- `WarningsAsErrors: '*'` — CI treats every enabled check as a hard error.
- `HeaderFilterRegex: '^(src|include|benchmarks)/'` — only report findings
  in first-party headers; suppress noise from system / third-party headers
  reached transitively.

### Python (`pyproject.toml`)

- `[tool.ruff]`: `line-length = 120`, `target-version = "py311"`.
- `[tool.ruff.lint]`: `select = ["E", "F", "W", "I", "B", "UP", "SIM", "PL"]`
  (pyflakes + isort + bugbear + pyupgrade + simplify + pylint subset).

## File scope

- **C++ formatter**: `src/**/*.{cpp,hpp}`, `include/**/*.hpp`,
  `benchmarks/**/*.{cpp,hpp}`, `tests/**/*.{cpp,hpp}`.
- **C++ linter**: `src/**/*.cpp`, `include/**/*.hpp`, `benchmarks/**/*.cpp`.
  **Excludes `tests/`** — gtest macros trip many lint rules.
- **Python**: `scripts/**/*.py`.
- **Excludes everywhere**: `build/`, `nix/`, `.git/`, any generated files.

## CI enforcement

Add a new `format-lint` job to `.github/workflows/ci.yml`, parallel to the
existing `build-test` matrix:

- Runs on `ubuntu-latest` only — formatter output is identical across
  platforms, so a matrix is wasteful.
- Steps:
  1. Checkout repo.
  2. `apt-get install clang-format clang-tidy`.
  3. `pip install ruff==<pinned version>` — pin to avoid surprise
     reformats on ruff upgrades.
  4. `cmake -S . -B build -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` — generates
     the compile commands database that clang-tidy needs.
  5. `./scripts/lint.sh` — the script is the **single source of truth** for
     formatting and lint commands. CI calls the same script contributors
     run locally; no separate command list duplicated in the workflow.

The job fails fast with the exact file and rule name for any violation.
By calling `lint.sh` directly, CI and local `lint.sh` cannot drift.

## Local ergonomics

### Devshell (`flake.nix`)

Add to `devShells.default.packages`:

- `pkgs.clang-tools` (provides `clang-format` and `clang-tidy`).
- `pkgs.ruff`.

### Convenience scripts

Both bash, executable, in `scripts/`:

- `scripts/format.sh` — applies `clang-format -i`, `ruff format`, and
  `ruff check --fix` across the in-scope file lists. Idempotent. Used
  locally by contributors before commit.
- `scripts/lint.sh` — runs `clang-format --dry-run -Werror`,
  `ruff format --check`, `ruff check`, and `clang-tidy -p build` over
  the same file lists. Read-only. **Invoked verbatim by CI** so CI and
  local checks cannot diverge.

Each script defines its file lists at the top as bash arrays, sourced or
duplicated between scripts as needed. The CI workflow only needs to
install deps, configure cmake (for `compile_commands.json`), and run
`./scripts/lint.sh`.

## One-time pass

Two separate commits, in order (per user direction):

1. **`chore: apply clang-format and ruff format across repo`**
   - Pure mechanical formatting. No semantic changes.
   - May touch many files; diff is whitespace and wrapping only.

2. **`chore: address clang-tidy and ruff lint findings`**
   - All lint findings fixed (or suppressed with `// NOLINT(rule-name)` /
     `# noqa: RULE` for legitimate false positives) in a single commit.
   - This commit may contain semantic changes — review per-file.
   - If a finding requires non-trivial refactoring, prefer `// NOLINT`
     with a follow-up note rather than expanding scope.

## README

Add a "Formatting and linting" section: tools enforced in CI, run
`./scripts/format.sh` to apply, `./scripts/lint.sh` to verify, all tools
in `nix develop`.

## Risks

- **clang-tidy noise**: the chosen check set may surface more findings
  than expected. Mitigation: if the lint-fix commit balloons, narrow
  `.clang-tidy` before pushing.
- **Header-only emission**: warnings from headers may be reported via
  consumer translation units. `HeaderFilterRegex` constrains this.
- **ruff version drift**: ruff updates regularly. Pin the version in
  CI (and document in `pyproject.toml` if useful) to avoid surprise
  reformatting.

## Out of scope

- Pre-commit framework / git hooks.
- `cppcoreguidelines-*` clang-tidy checks (high noise).
- Full pylint rule set (ruff `PL` subset is enough for now).
- CMake `format` target — `scripts/format.sh` is the entry point.
- Editor integration — `.clang-format` and `pyproject.toml` are
  auto-detected by most editors; no repo-level config required.

## Next step

Invoke `superpowers:writing-plans` to produce a task-by-task implementation
plan.

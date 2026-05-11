# Auto-Formatter and Linter Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Configure `clang-format` + `clang-tidy` for C++, `ruff format` + `ruff check` for Python, enforce both in CI via a single `scripts/lint.sh` that CI calls verbatim.

**Architecture:** Five config files at the repo root (`.clang-format`, `.clang-tidy`, `pyproject.toml`) and in `scripts/` (`format.sh`, `lint.sh`). The Nix devshell ships the tools. CI runs `cmake` to produce `compile_commands.json` then calls `./scripts/lint.sh` — no command-list duplication between local and CI. The one-time pass is two commits: a pure-format commit and a single lint-fix commit.

**Tech Stack:** clang-format, clang-tidy, ruff, GitHub Actions, Nix devshell, CMake.

---

## File map

| File | Action | Purpose |
|---|---|---|
| `.clang-format` | create | clang-format style |
| `.clang-tidy` | create | clang-tidy checks + warnings-as-errors |
| `pyproject.toml` | create | ruff config (`[tool.ruff]`, `[tool.ruff.lint]`) |
| `scripts/format.sh` | create | apply formatters + autofix in place |
| `scripts/lint.sh` | create | read-only check; called by CI verbatim |
| `flake.nix` | modify | add `clang-tools`, `ruff` to devshell |
| `.github/workflows/ci.yml` | modify | add `format-lint` job |
| `README.md` | modify | document the workflow |
| C++/Python sources | modify | one-time format + one-time lint-fix |

---

### Task 1: Add `clang-tools` and `ruff` to the Nix devshell

**Files:**
- Modify: `flake.nix:19-29`

- [ ] **Step 1: Edit `flake.nix` devShell packages**

Replace the `packages = [...]` block (currently lines 20-28) with:

```nix
          packages = [
            pkgs.cmake
            pkgs.ninja
            pkgs.clang
            pkgs.clang-tools
            pkgs.cli11
            pkgs.gtest
            pkgs.ruff
            sljit
            (pkgs.python3.withPackages (ps: [ ps.matplotlib ps.pandas ]))
          ];
```

- [ ] **Step 2: Reload devshell and verify both tools are present**

Run:
```bash
nix develop -c clang-format --version
nix develop -c clang-tidy --version
nix develop -c ruff --version
```

Expected: each prints a version line (no "command not found"). Record the printed `ruff --version` — it is the version you will pin in CI (Task 4 and Task 9).

- [ ] **Step 3: Commit**

```bash
git add flake.nix
git commit -m "$(cat <<'EOF'
build(nix): add clang-tools and ruff to devshell

Provides clang-format, clang-tidy, and ruff for local formatting and
lint checks. Matches the versions used by CI.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### Task 2: Create `.clang-format`

**Files:**
- Create: `.clang-format`

- [ ] **Step 1: Write the file**

Path: `.clang-format`

```yaml
---
BasedOnStyle: Google
ColumnLimit: 120
IndentWidth: 2
PointerAlignment: Left
DerivePointerAlignment: false
IncludeBlocks: Preserve
SortIncludes: CaseSensitive
---
```

`IncludeBlocks: Preserve` keeps the existing system / `extern "C"` / local include grouping intact — otherwise clang-format would merge them into a single sorted block and break the `extern "C" { sljitLir.h }` boundary in `src/main.cpp:13`.

- [ ] **Step 2: Verify the config parses**

Run:
```bash
nix develop -c clang-format --style=file --dump-config | head -10
```

Expected: prints a config dump starting with `BasedOnStyle: Google` etc. No "Invalid value" errors.

- [ ] **Step 3: Commit**

```bash
git add .clang-format
git commit -m "$(cat <<'EOF'
build: add .clang-format (Google, 120-col, preserve include blocks)

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### Task 3: Create `.clang-tidy`

**Files:**
- Create: `.clang-tidy`

- [ ] **Step 1: Write the file**

Path: `.clang-tidy`

```yaml
---
Checks: >
  bugprone-*,
  performance-*,
  readability-*,
  modernize-*,
  -modernize-use-trailing-return-type,
  -readability-identifier-length,
  -readability-magic-numbers
WarningsAsErrors: '*'
HeaderFilterRegex: '^(src|include|benchmarks)/'
FormatStyle: file
---
```

`-readability-identifier-length` and `-readability-magic-numbers` are disabled because they fire on essentially every benchmark with loop variables and tuning constants — out of scope for this pass.

- [ ] **Step 2: Smoke test**

Run:
```bash
nix develop -c cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
nix develop -c clang-tidy --config-file=.clang-tidy -p build src/axis.cpp
```

Expected: clang-tidy runs (may report findings or none). Should NOT print "error: no checks enabled" or "error: invalid configuration".

- [ ] **Step 3: Commit**

```bash
git add .clang-tidy
git commit -m "$(cat <<'EOF'
build: add .clang-tidy (bugprone/performance/readability/modernize)

Disables modernize-use-trailing-return-type, readability-identifier-length,
and readability-magic-numbers as they would flood the lint pass.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### Task 4: Create `pyproject.toml` with ruff config

**Files:**
- Create: `pyproject.toml`

- [ ] **Step 1: Write the file**

Use the ruff version you recorded in Task 1 Step 2 as `<ruff-version>` (substitute literally, e.g. `0.8.4`).

Path: `pyproject.toml`

```toml
[tool.ruff]
line-length = 120
target-version = "py311"
required-version = "<ruff-version>"

[tool.ruff.lint]
select = ["E", "F", "W", "I", "B", "UP", "SIM", "PL"]

[tool.ruff.format]
# defaults are fine; keep this section as a marker
```

`required-version` causes ruff to refuse to run if the binary version differs — this catches accidental version drift between devshell and CI.

- [ ] **Step 2: Verify ruff accepts the config**

Run:
```bash
nix develop -c ruff check --no-fix scripts/ || true
nix develop -c ruff format --check scripts/ || true
```

Expected: both commands run (may report findings). Should NOT print "invalid configuration" or "required-version mismatch".

- [ ] **Step 3: Commit**

```bash
git add pyproject.toml
git commit -m "$(cat <<'EOF'
build: add pyproject.toml with ruff config

Pins ruff version, sets 120-col line length, enables
pyflakes/isort/bugbear/pyupgrade/simplify/pylint-subset rules.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### Task 5: Create `scripts/format.sh`

**Files:**
- Create: `scripts/format.sh`

- [ ] **Step 1: Write the script**

Path: `scripts/format.sh`

```bash
#!/usr/bin/env bash
# Apply clang-format and ruff in place across the repo.
# Idempotent. Safe to run repeatedly.

set -euo pipefail

cd "$(dirname "$0")/.."

# C++ files: format everything under these trees.
mapfile -t CXX_FILES < <(
  find src include benchmarks tests \
    \( -name '*.cpp' -o -name '*.hpp' \) \
    -type f
)

if [ "${#CXX_FILES[@]}" -gt 0 ]; then
  clang-format -i "${CXX_FILES[@]}"
fi

# Python: format then autofix lint findings.
ruff format scripts/
ruff check --fix scripts/
```

- [ ] **Step 2: Make it executable**

Run:
```bash
chmod +x scripts/format.sh
```

- [ ] **Step 3: Smoke test (don't commit results yet)**

Run:
```bash
nix develop -c ./scripts/format.sh
git diff --stat
```

Expected: prints a list of files modified by formatting. **Do NOT commit this diff in this task** — the one-time format commit is Task 7. Reset the working tree:

```bash
git restore .
```

- [ ] **Step 4: Commit the script itself**

```bash
git add scripts/format.sh
git commit -m "$(cat <<'EOF'
chore(scripts): add format.sh — apply clang-format and ruff in place

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### Task 6: Create `scripts/lint.sh`

**Files:**
- Create: `scripts/lint.sh`

- [ ] **Step 1: Write the script**

Path: `scripts/lint.sh`

```bash
#!/usr/bin/env bash
# Read-only formatting + lint checks. Called verbatim by CI.
# Returns non-zero on any violation.

set -euo pipefail

cd "$(dirname "$0")/.."

# C++ files to format-check (includes tests).
mapfile -t CXX_FORMAT_FILES < <(
  find src include benchmarks tests \
    \( -name '*.cpp' -o -name '*.hpp' \) \
    -type f
)

# C++ files to lint with clang-tidy (excludes tests — gtest macros).
mapfile -t CXX_LINT_FILES < <(
  find src benchmarks -name '*.cpp' -type f
)

echo "==> clang-format --dry-run"
clang-format --dry-run --Werror "${CXX_FORMAT_FILES[@]}"

echo "==> ruff format --check"
ruff format --check scripts/

echo "==> ruff check"
ruff check scripts/

if [ ! -f build/compile_commands.json ]; then
  echo "lint.sh: build/compile_commands.json missing." >&2
  echo "        Run: cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON" >&2
  exit 1
fi

echo "==> clang-tidy"
clang-tidy --quiet -p build "${CXX_LINT_FILES[@]}"
```

- [ ] **Step 2: Make it executable**

Run:
```bash
chmod +x scripts/lint.sh
```

- [ ] **Step 3: Smoke test (expect failures pre-format-pass)**

Run:
```bash
nix develop -c cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
nix develop -c ./scripts/lint.sh || echo "expected: lint reports findings before the one-time pass"
```

Expected: lint.sh runs all four stages and may exit non-zero with specific findings. That is **expected** — we have not done the one-time pass yet.

- [ ] **Step 4: Commit the script itself**

```bash
git add scripts/lint.sh
git commit -m "$(cat <<'EOF'
chore(scripts): add lint.sh — read-only format + lint check

Used locally and called verbatim by CI to avoid command-list drift.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### Task 7: One-time formatting pass

**Files:**
- Modify: any C++ or Python source files that differ from formatter output.

- [ ] **Step 1: Run the formatter**

```bash
nix develop -c ./scripts/format.sh
```

- [ ] **Step 2: Inspect the diff**

Run:
```bash
git diff --stat
git diff | head -100
```

Verify the diff is whitespace/wrapping/include-ordering only — no semantic changes. If anything looks suspicious, stop and investigate before committing.

- [ ] **Step 3: Re-build and re-test to confirm no breakage**

```bash
nix develop -c cmake --build build
nix develop -c ctest --test-dir build --output-on-failure
```

Expected: all tests still pass (formatter must not have changed behavior).

- [ ] **Step 4: Verify format check now passes**

```bash
nix develop -c clang-format --dry-run --Werror $(find src include benchmarks tests \( -name '*.cpp' -o -name '*.hpp' \) -type f)
nix develop -c ruff format --check scripts/
```

Expected: both commands exit 0 with no diff.

- [ ] **Step 5: Commit**

```bash
git add -u
git commit -m "$(cat <<'EOF'
chore: apply clang-format and ruff format across repo

Mechanical reformat only; no semantic changes. Tests pass.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### Task 8: One-time lint-fix pass

**Files:**
- Modify: any source files with clang-tidy or ruff lint findings.

- [ ] **Step 1: Run the linters and capture findings**

```bash
nix develop -c ruff check scripts/ 2>&1 | tee /tmp/ruff-findings.txt || true
nix develop -c clang-tidy --quiet -p build $(find src benchmarks -name '*.cpp' -type f) 2>&1 | tee /tmp/clang-tidy-findings.txt || true
```

Read both files. For each finding, decide:
- **Fix**: if the change is a small, mechanical fix (e.g., `auto` over explicit type, `const` correctness, `std::move`).
- **Suppress**: if the change would require non-trivial refactoring. Add `// NOLINT(check-name)` on the offending line, or `# noqa: RULE` for ruff. Suppress narrowly — never use blanket suppressions.

- [ ] **Step 2: Apply autofixes where possible**

```bash
nix develop -c ruff check --fix scripts/
```

`ruff --fix` handles the safe autofixable rules (unused imports, sorted imports, etc.). Review the resulting diff.

For clang-tidy, manual edits guided by `/tmp/clang-tidy-findings.txt`. clang-tidy `--fix` is available but applies fixes invasively; prefer manual application per finding.

- [ ] **Step 3: Verify lint passes clean**

```bash
nix develop -c ./scripts/lint.sh
```

Expected: exit 0 with no findings.

- [ ] **Step 4: Re-build and re-test**

```bash
nix develop -c cmake --build build
nix develop -c ctest --test-dir build --output-on-failure
```

Expected: tests pass.

- [ ] **Step 5: Commit (single commit per user direction)**

```bash
git add -u
git commit -m "$(cat <<'EOF'
chore: address clang-tidy and ruff lint findings

Fixes and narrow NOLINT/noqa suppressions surfaced by the
first-time lint pass.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### Task 9: Add `format-lint` job to CI

**Files:**
- Modify: `.github/workflows/ci.yml`

- [ ] **Step 1: Add the job**

Use the ruff version you recorded in Task 1 Step 2 as `<ruff-version>` (must match `required-version` in `pyproject.toml`).

Append at the end of `.github/workflows/ci.yml`, after the existing `nix` job, at the same indentation as `build-test:` and `nix:`:

```yaml
  format-lint:
    name: format-lint
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: install apt deps
        run: |
          sudo apt-get update
          sudo apt-get install -y clang-format clang-tidy ninja-build

      - name: install ruff
        run: pip install ruff==<ruff-version>

      - name: configure (for compile_commands.json)
        run: cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

      - name: lint
        run: ./scripts/lint.sh
```

- [ ] **Step 2: Verify YAML is valid**

Run:
```bash
nix develop -c python3 -c "import yaml; yaml.safe_load(open('.github/workflows/ci.yml'))"
```

Expected: no exception.

- [ ] **Step 3: Commit**

```bash
git add .github/workflows/ci.yml
git commit -m "$(cat <<'EOF'
ci: add format-lint job

Runs clang-format/ruff format-check, ruff check, and clang-tidy via
scripts/lint.sh on every push and PR.

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### Task 10: Document in README

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Add the section**

Append a new section to `README.md` (place it near other developer-workflow content, e.g. after the build/test instructions):

```markdown
## Formatting and linting

Formatters and linters run in CI and must pass before merging.

- C++: `clang-format` (style in `.clang-format`) and `clang-tidy` (checks in `.clang-tidy`).
- Python: `ruff format` and `ruff check` (config in `pyproject.toml`).

Apply formatters locally:

```bash
./scripts/format.sh
```

Verify the way CI does:

```bash
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
./scripts/lint.sh
```

All tools are provided by `nix develop`.
```

- [ ] **Step 2: Commit**

```bash
git add README.md
git commit -m "$(cat <<'EOF'
docs(readme): document formatter and linter workflow

Co-Authored-By: Claude Opus 4.7 <noreply@anthropic.com>
EOF
)"
```

---

### Task 11: Final end-to-end verification

**Files:** none (verification only)

- [ ] **Step 1: Fresh local check**

```bash
/bin/rm -rf build
nix develop -c cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
nix develop -c ./scripts/lint.sh
nix develop -c cmake --build build
nix develop -c ctest --test-dir build --output-on-failure
```

Expected: lint.sh exits 0, build succeeds, all tests pass.

- [ ] **Step 2: Push and watch CI**

```bash
git push origin main
```

Then watch the `format-lint` job in the GitHub Actions UI (or `gh run watch`). Expected: all three jobs (`build-test`, `nix`, `format-lint`) green.

- [ ] **Step 3: If CI fails**

If `format-lint` fails on CI but not locally, the most likely cause is a ruff-version mismatch (devshell vs the version pinned in `pyproject.toml`/CI). Run `nix develop -c ruff --version` and compare to the version in `pyproject.toml`'s `required-version` and CI's `pip install ruff==X.Y.Z`. Update both pins to match the devshell.

---

## Recovery / rollback

If anything goes badly wrong mid-implementation, the format pass and lint-fix pass are each isolated commits. Revert with:

```bash
git revert <sha-of-bad-commit>
```

The config files (`.clang-format`, `.clang-tidy`, `pyproject.toml`) and scripts are independent of any source-file modifications; reverting the one-time pass commits does not break them.

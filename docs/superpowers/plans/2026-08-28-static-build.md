# Static Build Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a `-DFERRET_STATIC=ON` CMake option and a `packages.static` flake output that produce a fully static `ferret` binary, and publish prebuilt binaries for four targets from a new release workflow.

**Architecture:** Two decoupled mechanisms. `FERRET_STATIC` forces the FetchContent dependency path and adds `-static`; it is what CI's two musl cells use inside an Alpine container. `packages.static` changes nothing about how ferret builds and instead evaluates the existing derivation under `pkgs.pkgsStatic`, which supplies musl and `-static` from its stdenv. The two must never be combined; see Global Constraints.

**Tech Stack:** CMake 3.20+, Nix flakes (`pkgsStatic`), GitHub Actions, Docker (`alpine`), Android NDK.

**Spec:** `docs/superpowers/specs/2026-08-28-static-build-design.md`

## Global Constraints

- `FERRET_STATIC` defaults to `OFF`. Every existing build path, CI job, and timing-sensitive workflow must be byte-for-byte unaffected when the option is not set.
- Static linking is **Linux only**. `FERRET_STATIC=ON` must hard-error on Darwin: macOS ships no static libc and no `crt0.o`.
- `FERRET_STATIC=ON` combined with any `FERRET_SANITIZER` value must hard-error. Sanitizer runtimes require dynamic linking.
- **Nix must never pass `-DFERRET_STATIC=ON`.** The option forces FetchContent, FetchContent needs network, and the Nix sandbox forbids it. `nix/ferret.nix` and `nix/sljit.nix` are not modified by this plan.
- **Alpine runs via `docker run`, never `container:`.** The Actions runner injects a glibc-linked Node into job containers to execute `actions/checkout`; Alpine has no glibc and the job fails before compiling.
- Artifact names are exact: `ferret-linux-x86_64-musl`, `ferret-linux-aarch64-musl`, `ferret-android-arm64`, `ferret-macos-arm64-dynamic`.
- Workflow conventions, copied from the existing workflows: every action pinned to a full SHA with a trailing `  # vX.Y.Z` comment (two spaces before `#`), `persist-credentials: false` on every checkout, a `concurrency` group, `timeout-minutes` on every job, and a workflow-level `permissions: contents: read`.
- No new files under `scripts/`. The Alpine recipe lives in `release.yml` only.
- Commit messages: `type(scope): subject`, lowercase, imperative, no trailing period. **Forbidden:** AI tool names anywhere (including `Co-Authored-By:` footers) and process narration ("Task 3", "Phase 1", "FIXED").
- Commit with `--no-gpg-sign`. The repository's signing key is expired and unsigned commits are authorized here.
- `docs/superpowers/` is excluded from both prettier (`.prettierignore`) and markdownlint (`scripts/lint.sh:58`). This plan and its spec are lint-exempt; every other file this plan touches is not.
- Never run bare `git stash`: this is a worktree sharing a stash stack with other checkouts.

---

## File Structure

| File | Change | Responsibility |
| --- | --- | --- |
| `CMakeLists.txt` | Modify (2 sites) | Declares `FERRET_STATIC`, folds it into `_ferret_use_system_deps`, holds the guards and `-static` link flag |
| `flake.nix` | Modify (1 site) | Adds the Linux-only `packages.static` output |
| `.github/workflows/release.yml` | Create | Builds four artifacts; publishes on `v*` tags |
| `docs/build.md` | Modify | CMake-knob row + "Static builds" section |
| `README.md` | Modify | "Prebuilt binaries" pointer |
| `AGENTS.md` | Modify | CI-gates table row + footgun entry |
| `docs/android.md` | Modify | Note that the Android binary is downloadable |

**A note on testing style.** This is build-system work; there is no unit-test harness that can assert on a linker invocation. The TDD cycle here is *write the verification command, run it, watch it fail with a specific message, implement, run it again, watch it pass*. Every task below gives the exact command and the exact expected output for both halves of that cycle. Do not skip the "watch it fail" step, because on this kind of change it is the only thing distinguishing a working guard from a guard that never fires.

---

### Task 1: FERRET_STATIC CMake option

**Files:**

- Modify: `CMakeLists.txt:14` (add option), `CMakeLists.txt:16-19` (fold into `_ferret_use_system_deps`), `CMakeLists.txt:62` (add guards + link flag after the sanitizer block)
- Modify: `docs/build.md:58-64` (CMake knobs table)

**Interfaces:**

- Consumes: nothing; this is the first task.
- Produces: the CMake option `FERRET_STATIC` (BOOL, default `OFF`). Task 3 invokes it as `cmake -S . -B build-static -DFERRET_STATIC=ON -DCMAKE_BUILD_TYPE=Release`. No other task reads it.

**Prerequisite:** Docker must be running (Step 6 builds inside Alpine). On Apple Silicon this pulls the native arm64 image and produces a `linux/aarch64` static binary, which is a valid proof of the recipe.

- [ ] **Step 1: Run the guard checks and watch them NOT fire**

```bash
rm -rf /tmp/ferret-guard-darwin /tmp/ferret-guard-san
cmake -S . -B /tmp/ferret-guard-darwin -DFERRET_STATIC=ON 2>&1 | tail -5
cmake -S . -B /tmp/ferret-guard-san -DFERRET_STATIC=ON -DFERRET_SANITIZER=address 2>&1 | tail -5
```

Expected: **both succeed**, ending in `-- Generating done` / `-- Build files have been written to: ...`. CMake may also print `Manually-specified variables were not used by this project: FERRET_STATIC`, which is precisely the bug: the option does not exist yet, so the flag is silently ignored.

- [ ] **Step 2: Declare the option**

Insert immediately after the `option(FERRET_WERROR ...)` line at `CMakeLists.txt:14`:

```cmake

# --- Static linking ---
# Produces a binary with no runtime dependencies, for copying onto a
# machine whose glibc / libstdc++ versions are unknown. Linux only:
# Apple ships no static libc and no crt0.o, so -static cannot link on
# Darwin. Off by default, so timing-sensitive workflows keep the normal
# dynamic build. Only the option lives here, because the dependency
# switch below reads it; the guards and link flags sit further down,
# after FERRET_SANITIZER is declared.
option(FERRET_STATIC "Link ferret fully statically (no runtime dependencies)" OFF)
```

- [ ] **Step 3: Fold the option into the dependency switch**

Replace `CMakeLists.txt:16-19` in full:

```cmake
set(_ferret_use_system_deps ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  set(_ferret_use_system_deps OFF)
endif()
```

with:

```cmake
# System dependencies are bypassed for Android (nothing but the NDK
# sysroot is on the search path) and for static builds (a shared
# libspdlog.so cannot be linked into a -static binary; FetchContent
# builds every dependency as a .a instead).
set(_ferret_use_system_deps ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Android" OR FERRET_STATIC)
  set(_ferret_use_system_deps OFF)
endif()
```

- [ ] **Step 4: Add the guards and the link flag**

Insert after the sanitizer block's closing `endif()` at `CMakeLists.txt:62`, immediately before the `# --- GoogleTest: find_package first, FetchContent fallback ---` comment:

```cmake

# --- Static link flags ---
# Placed after FERRET_SANITIZER is declared so the conflict guard can
# read it, and before the FetchContent calls below so that vendored
# targets and everything under tests/ inherit -static. The sanitizer
# check runs before the Darwin check on purpose: it makes the conflict
# reportable on macOS instead of being masked by the platform error.
if(FERRET_STATIC)
  if(FERRET_SANITIZER)
    message(FATAL_ERROR "ferret: FERRET_STATIC=ON cannot be combined with FERRET_SANITIZER='${FERRET_SANITIZER}'. "
                        "Sanitizer runtimes require dynamic linking."
    )
  endif()
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    message(FATAL_ERROR "ferret: FERRET_STATIC=ON is not supported on Darwin. macOS ships no static libc and no "
                        "crt0.o, so a fully static executable cannot be linked. Build static binaries on Linux "
                        "(see docs/build.md)."
    )
  endif()
  message(STATUS "ferret: static linking enabled")
  add_link_options(-static)
endif()
```

- [ ] **Step 5: Run the guard checks again and watch them fire**

```bash
rm -rf /tmp/ferret-guard-darwin /tmp/ferret-guard-san
cmake -S . -B /tmp/ferret-guard-san -DFERRET_STATIC=ON -DFERRET_SANITIZER=address 2>&1 | tail -6
cmake -S . -B /tmp/ferret-guard-darwin -DFERRET_STATIC=ON 2>&1 | tail -6
```

Expected: the first exits non-zero with `CMake Error ... FERRET_STATIC=ON cannot be combined with FERRET_SANITIZER='address'`. The second exits non-zero with `CMake Error ... not supported on Darwin`.

If you are executing this on Linux rather than macOS, the second command will instead configure successfully, which is correct behaviour, not a failure. Confirm the Darwin branch by reading it; only the sanitizer guard is verifiable on Linux.

- [ ] **Step 6: Prove a static binary actually builds, links, tests, and runs**

```bash
docker run --rm -v "$PWD:/src" -w /src alpine:3.22 sh -c '
set -eu
apk add --no-cache build-base cmake git
cmake -S . -B build-static-check -DFERRET_STATIC=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-static-check -j"$(nproc)"
readelf -l build-static-check/ferret > /tmp/phdrs.txt
if grep -q INTERP /tmp/phdrs.txt; then
  echo "ERROR: ferret has an INTERP header - it is dynamically linked" >&2
  exit 1
fi
ctest --test-dir build-static-check --output-on-failure
./build-static-check/ferret run dependent_chain_throughput --reps=1 --warmup=0 --out=/tmp/smoke.csv
test "$(wc -l < /tmp/smoke.csv)" -ge 2
echo "STATIC BUILD OK"
'
```

Expected: ends with `STATIC BUILD OK`. All ctest cases pass.

Four things about this command are deliberate and should not be "cleaned up":

1. **No `-GNinja`.** Alpine's ninja packaging has shifted across releases (`ninja`, `ninja-build`, `samurai`); `build-base` always provides `make`. The generator has no bearing on whether the binary is static, so the default generator removes a failure mode for free.
2. **`readelf` to a file, then `grep`.** Writing `readelf ... | grep -q INTERP && exit 1` inverts under `set -e`: in the *good* case grep exits non-zero, the `&&` chain returns non-zero, and the script dies claiming success failed. Worse, if `readelf` itself errors the pipeline still reports "static". Landing the output in a file makes `set -e` catch a broken `readelf` and leaves `grep` as a clean boolean.
3. **The smoke run is not redundant with ctest.** ctest proves the binary starts. Only a real `ferret run` proves sljit's `mmap` of executable pages still works under static musl, which is the single most likely thing to break.
4. **`--out=/tmp/smoke.csv`, not a path in the repo.** `.gitignore` ignores `*.csv`, but the bind mount would leave the file in your working tree regardless.

If the link step fails with an error about mixing `-static` and `-pie`,
the toolchain's specs default to PIE and `-static` needs `-no-pie`
alongside it. Alpine is not expected to need this; a contributor's host
distribution might. If it happens, add `-no-pie` to the
`add_link_options` call from Step 4 and note it in the task report.
Do not silently widen the flag set beyond that.

Clean up afterwards, since the directory is bind-mounted into your checkout:

```bash
rm -rf build-static-check
```

- [ ] **Step 7: Confirm the default build is untouched**

```bash
rm -rf build
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Expected: all pass, exactly as before the change. `-- ferret: static linking enabled` must **not** appear in the configure output.

- [ ] **Step 8: Format and lint the CMake changes**

```bash
cmake-format -i CMakeLists.txt
cmake-format --check CMakeLists.txt
cmake-lint CMakeLists.txt
git diff CMakeLists.txt
```

Expected: `--check` and `cmake-lint` both exit zero. Review the `cmake-format -i` reflow, then accept whatever it produces; it owns wrapping, and `.cmake-format.yaml` sets `line_width: 120` with `dangle_parens: true`.

- [ ] **Step 9: Document the knob**

Add this row to the CMake knobs table in `docs/build.md`, after the `-DFERRET_SANITIZER=<mode>` row:

```markdown
| `-DFERRET_STATIC=ON\|OFF`            | `OFF`   | Link fully statically, so the binary has no runtime dependencies. Linux only; errors on macOS and when combined with `FERRET_SANITIZER`. Forces the FetchContent dependency path, so a static configure needs network access. |
```

Then re-align the table and verify:

```bash
prettier --write docs/build.md
prettier --check docs/build.md
markdownlint-cli2 docs/build.md
```

Expected: both exit zero.

- [ ] **Step 10: Commit**

```bash
git add CMakeLists.txt docs/build.md
git commit --no-gpg-sign -m "build: add FERRET_STATIC option for fully static linking"
```

---

### Task 2: packages.static flake output

**Files:**

- Modify: `flake.nix:20-23` (the `let` block) and `flake.nix:77-80` (the `packages` attribute)

**Interfaces:**

- Consumes: nothing from Task 1. This task must **not** reference `FERRET_STATIC`; see Global Constraints.
- Produces: `packages.static` on `x86_64-linux` and `aarch64-linux` only. Nothing later in this plan depends on it; CI's artifacts come from Task 3, not from Nix.

- [ ] **Step 1: Confirm the output does not exist yet**

```bash
nix flake show --all-systems 2>&1 | grep -A3 'x86_64-linux'
```

Expected: under `packages.x86_64-linux` only `default` is listed. No `static`.

- [ ] **Step 2: Determine whether `nix flake check` builds packages**

This answers the spec's one open risk before you write any code, and the answer decides nothing in this task; it decides whether Task 6 needs to raise a timeout.

```bash
nix flake check -v 2>&1 | tail -30
```

Watch whether `packages.<system>.default` (the ferret derivation) is built or merely evaluated. Record the answer in your report for this task. If packages **are** built, the `nix.yml` job will start building `packages.static` on Linux once Step 3 lands, and you must check its runtime against the existing `timeout-minutes: 60` when CI first runs.

- [ ] **Step 3: Add the static package**

In `flake.nix`, extend the `let` block at lines 20-23 to:

```nix
      system: let
        pkgs = nixpkgs.legacyPackages.${system};
        sljit = pkgs.callPackage ./nix/sljit.nix {src = sljit-src;};
        sljitStatic = pkgs.pkgsStatic.callPackage ./nix/sljit.nix {src = sljit-src;};
      in {
```

and replace the `packages.default` attribute at lines 77-80 with:

```nix
        packages =
          {
            default = pkgs.callPackage ./nix/ferret.nix {
              inherit sljit;
              src = self;
            };
          }
          # pkgsStatic is musl + -static on Linux. On Darwin it cannot
          # produce a fully static executable (no static libc, no
          # crt0.o), so the output is Linux-only. Note this deliberately
          # does NOT pass -DFERRET_STATIC=ON: that option forces the
          # FetchContent path, which needs network access the Nix
          # sandbox does not grant. pkgsStatic's stdenv supplies the
          # static linking, and its spdlog/gtest/cli11 are .a archives
          # the existing find_package path resolves unchanged.
          // pkgs.lib.optionalAttrs pkgs.stdenv.isLinux {
            static = pkgs.pkgsStatic.callPackage ./nix/ferret.nix {
              sljit = sljitStatic;
              src = self;
            };
          };
```

Match the surrounding formatting: this file uses compact braces (`{src = sljit-src;}`) rather than the `nixfmt-rfc-style` the `formatter` attribute names. Nothing in `scripts/lint.sh` or `nix flake check` gates Nix formatting, so follow the file, not the formatter.

- [ ] **Step 4: Verify the output appears for Linux and not for Darwin**

```bash
nix flake show --all-systems 2>&1 | grep -B1 -A4 'packages'
```

Expected: `static` and `default` listed under both `packages.x86_64-linux` and `packages.aarch64-linux`; only `default` under `packages.x86_64-darwin` and `packages.aarch64-darwin`.

- [ ] **Step 5: Verify the Linux derivation evaluates**

```bash
nix eval --raw .#packages.x86_64-linux.static.drvPath
nix eval --raw .#packages.aarch64-linux.static.drvPath
```

Expected: each prints a `/nix/store/....drv` path. This proves the expression is well-formed without building it.

- [ ] **Step 6: Verify nothing on the current system regressed**

```bash
nix flake check
nix build .#default && ./result/bin/ferret list
```

Expected: `nix flake check` exits zero; `ferret list` prints the four registered benchmark names.

**Honest limitation to report:** on macOS you cannot build `packages.static`, because there is no Linux builder configured. Its first real build happens in CI. Say so explicitly in your task report rather than implying it was verified.

- [ ] **Step 7: Commit**

```bash
git add flake.nix
git commit --no-gpg-sign -m "build: add static flake package built from pkgsStatic"
```

---

### Task 3: Release workflow, musl static Linux cells

**Files:**

- Create: `.github/workflows/release.yml`

**Interfaces:**

- Consumes: `-DFERRET_STATIC=ON` from Task 1.
- Produces: a `build` job with a `matrix.include` list whose entries carry `artifact`, `os`, and `kind` keys, and a job-level `ALPINE_IMAGE` env var. Task 4 appends two entries with `kind: android` and `kind: macos` to that same matrix. Task 5 adds a `publish` job with `needs: build` that consumes the uploaded artifact names.

- [ ] **Step 1: Resolve the Alpine image digest**

Never pin by tag: the repository pins every GitHub Action by SHA and the container image gets the same treatment.

```bash
docker pull alpine:3.22
docker inspect --format='{{index .RepoDigests 0}}' alpine:3.22
```

Expected: a line like `alpine@sha256:<64 hex chars>`. Use that exact string as `ALPINE_IMAGE` below.

- [ ] **Step 2: Create the workflow with the two musl cells**

Create `.github/workflows/release.yml`. Substitute the digest from Step 1 for `<DIGEST>`:

```yaml
name: release

on:
  push:
    tags: ["v*"]
  pull_request:
    branches: [main]
  workflow_dispatch:

permissions:
  contents: read

concurrency:
  group: ${{ github.workflow }}-${{ github.ref }}
  cancel-in-progress: true

jobs:
  build:
    name: build (${{ matrix.artifact }})
    runs-on: ${{ matrix.os }}
    timeout-minutes: 45
    strategy:
      fail-fast: false
      matrix:
        include:
          - { artifact: ferret-linux-x86_64-musl, os: ubuntu-latest, kind: musl }
          - { artifact: ferret-linux-aarch64-musl, os: ubuntu-24.04-arm, kind: musl }
    env:
      # alpine:3.22
      ALPINE_IMAGE: alpine@sha256:<DIGEST>
      ARTIFACT: ${{ matrix.artifact }}
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1  # v7.0.1
        with:
          persist-credentials: false

      # Alpine runs via `docker run`, not `container:`. The Actions
      # runner injects a glibc-linked Node into job containers to run
      # actions/checkout, and Alpine has no glibc, so a container: job
      # fails before it compiles anything. Checkout and upload stay on
      # the glibc host; only the build and test happen in musl.
      - name: build and test static binary
        if: matrix.kind == 'musl'
        run: |
          docker run --rm -v "$PWD:/src" -w /src "$ALPINE_IMAGE" sh -c '
          set -eu
          apk add --no-cache build-base cmake git
          cmake -S . -B build-static -DFERRET_STATIC=ON -DCMAKE_BUILD_TYPE=Release
          cmake --build build-static -j"$(nproc)"
          readelf -l build-static/ferret > /tmp/phdrs.txt
          if grep -q INTERP /tmp/phdrs.txt; then
            echo "ERROR: ferret has an INTERP header - it is dynamically linked" >&2
            exit 1
          fi
          ctest --test-dir build-static --output-on-failure
          ./build-static/ferret run dependent_chain_throughput --reps=1 --warmup=0 --out=/tmp/smoke.csv
          test "$(wc -l < /tmp/smoke.csv)" -ge 2
          '
          sudo chown -R "$(id -u):$(id -g)" build-static
          cp build-static/ferret "$ARTIFACT"

      - name: upload artifact
        uses: actions/upload-artifact@043fb46d1a93c77aae656e7c1c64a875d1fc6a0a  # v7.0.1
        with:
          name: ${{ matrix.artifact }}
          path: ${{ matrix.artifact }}
          retention-days: 7
          if-no-files-found: error
```

Three details that are load-bearing:

- The `sh -c '...'` body is **single-quoted**, so `$(nproc)` and `$(wc -l ...)` reach the container shell unexpanded. `"$ALPINE_IMAGE"` sits outside the quotes and is expanded by the host. Do not convert the outer quotes to double quotes.
- `matrix.artifact` is passed through the `ARTIFACT` env var rather than interpolated directly into `run:`. `zizmor.yml` flags `${{ }}` expressions inside `run:` blocks as template-injection risks; the env indirection is the standard fix.
- Files the container writes are root-owned on a Linux runner, hence the `chown` before `cp`. This is not needed locally on Docker Desktop, which maps the host user, but it is required in CI.

- [ ] **Step 3: Validate the YAML parses**

```bash
prettier --check .github/workflows/release.yml
```

Expected: exit zero. If it reports a difference, run `prettier --write .github/workflows/release.yml` and re-check.

- [ ] **Step 4: Commit and push, then watch the workflow**

```bash
git add .github/workflows/release.yml
git commit --no-gpg-sign -m "ci: add release workflow with musl static linux builds"
git push
```

Then open the PR (or push to the existing one) and watch the `release` workflow. Expected: two green cells, `build (ferret-linux-x86_64-musl)` and `build (ferret-linux-aarch64-musl)`, each with an uploaded artifact.

If a cell fails, read the log before changing anything. The three failures worth anticipating: an `apk` package name change, the `chown` step failing because `sudo` is unavailable on the arm64 runner image, and the smoke run failing on `mlockall`. Note that ferret is expected to *warn and continue* there, so an actual failure means something else.

---

### Task 4: Release workflow, Android and macOS cells

**Files:**

- Modify: `.github/workflows/release.yml` (matrix `include` list, plus three new steps)

**Interfaces:**

- Consumes: the `build` job matrix and the `ARTIFACT` env var from Task 3.
- Produces: two more uploaded artifacts, `ferret-android-arm64` and `ferret-macos-arm64-dynamic`. Task 5 attaches all four to the release.

Neither cell sets `FERRET_STATIC`. Android gets portability from the NDK's default `c++_static` plus on-device bionic; macOS cannot be static at all.

- [ ] **Step 1: Extend the matrix**

Add two entries to the `matrix.include` list:

```yaml
          - { artifact: ferret-android-arm64, os: ubuntu-latest, kind: android }
          - { artifact: ferret-macos-arm64-dynamic, os: macos-latest, kind: macos }
```

- [ ] **Step 2: Add the Android steps**

Insert after the `build and test static binary` step and before `upload artifact`. This mirrors `.github/workflows/build.yml:90-109`:

```yaml
      - name: locate Android NDK
        if: matrix.kind == 'android'
        run: |
          # ubuntu-latest runners ship the Android NDK at ANDROID_NDK_LATEST_HOME.
          # Fail fast with a useful message if that assumption breaks.
          if [ -z "${ANDROID_NDK_LATEST_HOME:-}" ] || [ ! -f "$ANDROID_NDK_LATEST_HOME/build/cmake/android.toolchain.cmake" ]; then
            echo "ANDROID_NDK_LATEST_HOME not usable: '$ANDROID_NDK_LATEST_HOME'" >&2
            exit 1
          fi
          echo "ANDROID_NDK_HOME=$ANDROID_NDK_LATEST_HOME" >> "$GITHUB_ENV"

      - name: build (android)
        if: matrix.kind == 'android'
        run: |
          sudo apt-get update
          sudo apt-get install -y ninja-build
          cmake -S . -B build-android -GNinja \
            -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
            -DANDROID_ABI=arm64-v8a \
            -DANDROID_PLATFORM=android-35 \
            -DCMAKE_BUILD_TYPE=Release
          cmake --build build-android
          cp build-android/ferret "$ARTIFACT"
```

There is no test step. Android binaries cannot execute on the host, matching what `android-cross-build` already does in `build.yml`.

- [ ] **Step 3: Add the macOS step**

Insert after the Android steps:

```yaml
      - name: build and test (macos)
        if: matrix.kind == 'macos'
        run: |
          brew install ninja
          cmake -S . -B build-release -GNinja -DCMAKE_BUILD_TYPE=Release
          cmake --build build-release
          ctest --test-dir build-release --output-on-failure
          cp build-release/ferret "$ARTIFACT"
```

- [ ] **Step 4: Validate and commit**

```bash
prettier --check .github/workflows/release.yml
git add .github/workflows/release.yml
git commit --no-gpg-sign -m "ci: add android and macos cells to release workflow"
git push
```

Expected on CI: four green cells, four uploaded artifacts.

---

### Task 5: Publish job

**Files:**

- Modify: `.github/workflows/release.yml` (append a `publish` job)

**Interfaces:**

- Consumes: the four artifacts uploaded by the `build` job in Tasks 3 and 4.
- Produces: a GitHub Release on `v*` tags carrying five assets: the four binaries plus `SHA256SUMS`.

- [ ] **Step 1: Resolve the download-artifact SHA**

`actions/download-artifact` is not yet used in this repository, so there is no existing pin to copy.

```bash
gh api repos/actions/download-artifact/commits/v6 --jq '.sha'
gh api repos/actions/download-artifact/releases/latest --jq '.tag_name'
```

Expected: a 40-character SHA and a tag like `v6.0.0`. If `v6` 404s, try `v5`, and use whatever tag the second command reports for the trailing comment.

- [ ] **Step 2: Append the publish job**

Add at the end of `release.yml`, substituting the SHA and tag from Step 1:

```yaml
  publish:
    name: publish release
    needs: build
    if: startsWith(github.ref, 'refs/tags/v')
    runs-on: ubuntu-latest
    timeout-minutes: 15
    permissions:
      contents: write
    steps:
      - name: download artifacts
        uses: actions/download-artifact@<SHA>  # <TAG>
        with:
          path: artifacts
          merge-multiple: true

      - name: generate checksums
        run: |
          cd artifacts
          chmod +x ferret-*
          sha256sum ferret-* > SHA256SUMS
          cat SHA256SUMS

      - name: write release notes
        run: |
          cat > NOTES.md <<'EOF'
          ## Binaries

          | File | Target | Runtime dependencies |
          | --- | --- | --- |
          | `ferret-linux-x86_64-musl` | Linux x86_64 | none, fully static (musl) |
          | `ferret-linux-aarch64-musl` | Linux aarch64 | none, fully static (musl) |
          | `ferret-android-arm64` | Android arm64-v8a | on-device bionic only |
          | `ferret-macos-arm64-dynamic` | macOS arm64 | **dynamically linked** against the macOS SDK |

          macOS cannot produce a fully static executable, because Apple ships no
          static libc and no `crt0.o`. That binary alone therefore carries no
          no-runtime-dependencies guarantee.

          Downloads are not marked executable. After downloading:

          ```sh
          chmod +x ferret-linux-x86_64-musl
          ./ferret-linux-x86_64-musl list
          ```

          Verify with `sha256sum -c SHA256SUMS`.
          EOF

      - name: create release
        env:
          GH_TOKEN: ${{ github.token }}
          TAG: ${{ github.ref_name }}
          REPO: ${{ github.repository }}
        run: |
          gh release create "$TAG" \
            --repo "$REPO" \
            --title "$TAG" \
            --notes-file NOTES.md \
            artifacts/ferret-* artifacts/SHA256SUMS
```

Notes on the choices here:

- `gh` rather than a third-party release action: nothing new to pin and audit, and less for `zizmor.yml` to object to. `gh release create --repo` needs no checkout, which is why this job has no `actions/checkout` step, a genuine least-privilege win.
- `contents: write` is scoped to this job alone; the workflow-level default stays `contents: read`.
- The heredoc body must keep uniform indentation. YAML strips the common leading whitespace from a `|` block, so the shell sees `cat`, the body, and `EOF` all at column 0. Do not re-indent `EOF` to column 0 inside the YAML; that breaks the block scalar.
- `sha256sum ferret-*` cannot match `SHA256SUMS`, because the glob is prefixed, so there is no ordering hazard between the two steps.
- The `chmod +x` is cosmetic for release assets, which carry no permission bits over HTTP. It matters for anyone pulling the PR-run zip, and the release notes tell downloaders to `chmod +x` regardless.

- [ ] **Step 3: Validate and commit**

```bash
prettier --check .github/workflows/release.yml
git add .github/workflows/release.yml
git commit --no-gpg-sign -m "ci: publish release artifacts on version tags"
git push
```

- [ ] **Step 4: Confirm the publish job is correctly skipped on the PR**

On the pull request run, the `publish` job must show as skipped while all four `build` cells run. Expected: `publish` greyed out with "This job was skipped".

Do **not** push a `v*` tag to test the publish path unless the user asks for it. Cutting a release is theirs to decide.

---

### Task 6: Documentation

**Files:**

- Modify: `docs/build.md` (new "Static builds" section), `README.md` (prebuilt binaries), `AGENTS.md` (CI table + footgun), `docs/android.md` (prebuilt note)

**Interfaces:**

- Consumes: the `FERRET_STATIC` option from Task 1, `packages.static` from Task 2, and the artifact names from Tasks 3-5.
- Produces: nothing consumed by other tasks.

The knob-table row in `docs/build.md` already landed in Task 1. This task adds the prose.

- [ ] **Step 1: Add the "Static builds" section to `docs/build.md`**

Insert after the "CMake knobs" table and before the "Sanitizer builds" section:

````markdown
## Static builds

`-DFERRET_STATIC=ON` links `ferret` with no runtime dependencies, so
the binary can be copied onto a machine whose glibc and libstdc++
versions are unknown. Three constraints:

- **Linux only.** macOS ships no static libc and no `crt0.o`, so the
  option hard-errors on Darwin. This is not a limitation to work
  around.
- **Incompatible with `FERRET_SANITIZER`.** Sanitizer runtimes require
  dynamic linking; combining the two hard-errors.
- **Needs network at configure time.** `FERRET_STATIC=ON` forces the
  FetchContent path even on a machine that has all four dependencies
  installed, because a shared `libspdlog.so` cannot be linked into a
  static binary.

Building against musl, which is what the published release artifacts use,
needs no toolchain setup beyond Docker:

```sh
docker run --rm -v "$PWD:/src" -w /src alpine:3.22 sh -c '
  apk add --no-cache build-base cmake git
  cmake -S . -B build-static -DFERRET_STATIC=ON -DCMAKE_BUILD_TYPE=Release
  cmake --build build-static -j"$(nproc)"
'
```

`.github/workflows/release.yml` holds the authoritative version of this
recipe, with a digest-pinned image and the full verification sequence;
the snippet above is a convenience copy. Confirm the result is static
with `readelf -l build-static/ferret`. A static binary has no `INTERP`
program header.

Nix users get the same thing from the flake, built against musl from
`pkgsStatic` and pinned by `flake.lock`:

```sh
nix build .#static
```

That output is Linux-only for the same reason the CMake option is.
````

- [ ] **Step 2: Add "Prebuilt binaries" to `README.md`**

Insert immediately before the `## Quickstart` heading:

````markdown
## Prebuilt binaries

Each release publishes binaries for four targets. The two Linux builds
are fully static against musl. Copy one onto any machine of the right
architecture and run it, with no toolchain, no Nix, and no matching
glibc.

```sh
curl -LO https://github.com/<owner>/ferret/releases/latest/download/ferret-linux-x86_64-musl
chmod +x ferret-linux-x86_64-musl
./ferret-linux-x86_64-musl list
```

`ferret-android-arm64` depends only on on-device bionic.
`ferret-macos-arm64-dynamic` is dynamically linked, because macOS
cannot produce a static executable. Verify any download against the release's
`SHA256SUMS`.

Building from source instead: [`docs/build.md`](docs/build.md).
````

Replace `<owner>` with the actual GitHub owner, read from `git remote get-url origin`.

- [ ] **Step 3: Update `AGENTS.md`**

Add to the CI workflow table, after the `nix.yml` row:

```markdown
| `release.yml`    | Static musl builds (linux x86_64 + aarch64), android + macos builds; publishes binaries on `v*` tags |
```

Add to the Footguns list:

```markdown
- **`FERRET_STATIC=ON` forces the FetchContent path.** A static configure needs network access even on a machine with all four dependencies installed, because a shared `libspdlog.so` cannot be linked into a static binary. It is also Linux-only and hard-errors when combined with `FERRET_SANITIZER`.
```

- [ ] **Step 4: Update `docs/android.md`**

Insert after the opening paragraph, before "## Prerequisites":

```markdown
Cross-compiling is optional. Each release publishes a prebuilt
`ferret-android-arm64`; download it, `chmod +x`, and skip straight to
[Stage Binaries On The Device](#stage-binaries-on-the-device). Build
from source when you need a modified ferret on the phone.
```

- [ ] **Step 5: Format and lint every touched doc**

```bash
prettier --write README.md AGENTS.md docs/build.md docs/android.md
prettier --check '**/*.md'
markdownlint-cli2 '**/*.md' '#build' '#_deps' '#node_modules' '#docs/superpowers'
```

Expected: both exit zero. Note that `docs/build.md` now contains a fenced code block inside a section that itself uses fences, so check the nesting rendered correctly by reading the file after prettier runs.

- [ ] **Step 6: Run the full pre-PR gate**

```bash
./scripts/format.sh
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build
ctest --test-dir build --output-on-failure
./scripts/test_py.sh
./scripts/lint.sh
```

Expected: all six exit zero. This is the gate from `AGENTS.md`; nothing merges without it.

- [ ] **Step 7: Commit**

```bash
git add README.md AGENTS.md docs/build.md docs/android.md
git commit --no-gpg-sign -m "docs: document static builds and prebuilt binaries"
git push
```

---

## Verification Summary

What proves this works, and where:

| Claim | Proven by | Where it runs |
| --- | --- | --- |
| The Darwin guard fires | Task 1 Step 5 | Locally, macOS only |
| The sanitizer guard fires | Task 1 Step 5 | Locally, any platform |
| A static binary links | Task 1 Step 6, `readelf` has no `INTERP` | Local Docker + both CI musl cells |
| The static binary's tests pass | `ctest` inside Alpine | Local Docker + both CI musl cells |
| JIT works under static musl | The `ferret run` smoke step | Local Docker + both CI musl cells |
| The default build is unaffected | Task 1 Step 7 + the untouched `build.yml` matrix | Local + CI |
| `packages.static` evaluates | Task 2 Steps 4-5 | Locally |
| `packages.static` **builds** | `nix.yml`, if `nix flake check` builds packages | CI only, unverifiable on macOS |
| The publish job skips on PRs | Task 5 Step 4 | CI |
| The publish job **works** | Nothing in this plan | Unverified until a real `v*` tag is cut |

The last two rows are the honest gaps. Publishing is verified only by cutting a release, which is the user's call, not the implementer's.

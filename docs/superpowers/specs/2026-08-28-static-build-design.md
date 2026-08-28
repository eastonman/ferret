# Static Build Design

## Goal

Produce a `ferret` binary that can be copied onto a machine you do not
control, such as a colleague's server, a lab box, or a phone, and run with no
runtime dependencies and no glibc or libstdc++ version matching. Ship
those binaries as published release artifacts so the common case is
downloading one rather than building it.

Two mechanisms serve this, deliberately decoupled:

- `-DFERRET_STATIC=ON`, a CMake option any contributor can use on any
  Linux, with no Nix and no container required.
- `packages.static`, a flake output built from `pkgs.pkgsStatic` for
  Nix users who want a store-pinned static build.

CI's two static cells build with the first; the Android and macOS
cells set no static flag at all. The second exists for Nix users and
is validated by `nix flake check`.

## Scope

In scope: a CMake static option, a static flake package, a release
workflow publishing four artifacts, and the documentation for all
three.

Out of scope: `-static-pie`, cross-compilation of any kind, a
`scripts/build_static.sh` wrapper, and any change to the default
(dynamic) build. `FERRET_STATIC` defaults to OFF; every existing build
path, CI job, and timing-sensitive workflow is unchanged.

## Platform Reality

The design is shaped by three facts established before it was written.

**Ferret has none of the usual static-linking hazards.** `src/`,
`include/`, and `benchmarks/` contain no `dlopen`, no `dlsym`, no
`getaddrinfo`, no `getpwnam`, and no other NSS entry point. The
failure modes that make static glibc a bad default do not apply here.
musl is still preferred for the published artifacts, but the choice is
about binary size and kernel-floor cleanliness, not correctness.

**macOS cannot produce a fully static executable.** Apple ships no
static libc and no `crt0.o`; both `-static` and `-static-libstdc++`
fail on Apple Clang. This is not a limitation to engineer around.
Darwin is excluded from the static path entirely, and its release
artifact is an ordinary dynamically-linked binary, named so that this
is obvious to anyone downloading it.

**Android is already close to portable.** The NDK defaults to
`c++_static`, and bionic is guaranteed present on-device. The Android
artifact is built with the existing cross-build recipe and does not
set `FERRET_STATIC`; adding `-static` there would buy almost nothing.

## CMake: FERRET_STATIC

```cmake
option(FERRET_STATIC "Link ferret fully statically (no runtime dependencies)" OFF)
```

Declared near the top of `CMakeLists.txt`, before dependency
resolution. When ON it has four effects.

**Forces the vendored-dependency path.** `_ferret_use_system_deps`
becomes OFF. A system shared `libspdlog.so` cannot be linked into a
`-static` binary; FetchContent builds all four dependencies as static
archives, since `BUILD_SHARED_LIBS` is off by default and
`gtest_force_shared_crt` is MSVC-only. Android already flips this same
switch, so `_ferret_use_system_deps` should be computed once from
`Android OR static` rather than assigned in two places.

**Adds `add_link_options(-static)`.** Directory-scoped, so vendored
targets and everything under `add_subdirectory(tests)` inherit it.
This is the same mechanism the sanitizer block already uses, and for
the same reason. It must appear above the `FetchContent_MakeAvailable`
calls for that inheritance to hold.

**Hard-errors on Darwin.** The message names the reason, no static
libc and no `crt0.o`, then points at the Linux path, so the failure is
self-explanatory rather than a link error.

**Hard-errors when `FERRET_SANITIZER` is also set.** ASan and TSan
runtimes require dynamic linking. Producing a static binary with
silently absent instrumentation would be worse than refusing.

`sljit_static` keeps its `-fPIC`; PIC objects link into a non-PIE
static executable without issue.

## Nix: packages.static

The CMake option and the Nix package must stay decoupled.
`FERRET_STATIC=ON` forces the FetchContent path, FetchContent needs
network access, and the Nix sandbox forbids it. Passing the flag from
a derivation would break the build.

The correct approach changes nothing about how ferret is built and
instead evaluates the existing derivation in a different package set:

```nix
sljitStatic = pkgs.pkgsStatic.callPackage ./nix/sljit.nix { src = sljit-src; };
packages.static = pkgs.pkgsStatic.callPackage ./nix/ferret.nix {
  sljit = sljitStatic;
  src = self;
};
```

`pkgsStatic`'s stdenv supplies musl and `-static`. Its `spdlog`,
`gtest`, and `cli11` are static archives that the existing
`find_package`-first logic in `CMakeLists.txt` resolves without
modification. `nix/sljit.nix` works unchanged: `$CC` and `$AR` become
the musl-static wrappers. `doCheck = true` is inherited, so ctest runs
against the static binary as part of the build.

Guarded with `lib.optionalAttrs pkgs.stdenv.isLinux`, since
`eachDefaultSystem` also covers `x86_64-darwin` and `aarch64-darwin`
where `pkgsStatic` cannot deliver a fully static executable.

Expected outcome: `nix/ferret.nix` and `nix/sljit.nix` are untouched;
the change is confined to `flake.nix`.

## CI: release.yml

A new workflow with four build cells and one publish job.

| Artifact                      | Runner             | Method                       |
| ----------------------------- | ------------------ | ---------------------------- |
| `ferret-linux-x86_64-musl`    | `ubuntu-latest`    | Alpine, `-DFERRET_STATIC=ON` |
| `ferret-linux-aarch64-musl`   | `ubuntu-24.04-arm` | Alpine, `-DFERRET_STATIC=ON` |
| `ferret-android-arm64`        | `ubuntu-latest`    | existing NDK recipe          |
| `ferret-macos-arm64-dynamic`  | `macos-latest`     | native Release build         |

The macOS artifact carries `-dynamic` in its name deliberately, and
the release body states that it is the one artifact without the
no-runtime-dependencies guarantee.

### Alpine runs under docker run, not container:

A job-level `container: alpine` does not work. The Actions runner
injects its own glibc-linked Node into job containers to execute
`actions/checkout`, and Alpine has no glibc, so the job fails before
compiling anything. Installing `nodejs` inside the image does not
address this.

Alpine is therefore invoked as an ordinary step. Checkout, artifact
upload, and every other action run on the glibc host; only the compile
and test run in the container:

```sh
docker run --rm -v "$PWD:/src" -w /src alpine:3.22 sh -c '
  apk add --no-cache build-base cmake ninja git
  cmake -S . -B build-static -GNinja -DFERRET_STATIC=ON -DCMAKE_BUILD_TYPE=Release
  cmake --build build-static
  ...'
```

The tag above is illustrative. The workflow pins the image by digest
with a trailing version comment, mirroring how the repository pins
GitHub Actions.

This also keeps the arm64 cell honest: `alpine` resolves to the native
arm64 image on `ubuntu-24.04-arm`, so there is no qemu emulation and
no cross-compilation anywhere in the design.

Files written inside the container are root-owned. The host needs a
`chown` before `upload-artifact`.

### Verification and testing per cell

Assert the absence of a dynamic-loader program header rather than
parsing `file` or `ldd` output:

```sh
readelf -l build-static/ferret | grep -q INTERP && { echo "not static"; exit 1; }
```

`readelf` is already present in `build-base`.

`ctest` runs inside the container for both Linux cells and natively on
macOS. The Android cell builds only; its binaries cannot execute on
the host, matching the existing `android-cross-build` job.

Both static cells additionally perform one real benchmark run, a
minimal-reps `ferret run dependent_chain_throughput`. ctest proves the
binary starts; only a real run proves sljit's `mmap` of executable
pages still works under static musl. This is the highest-value single
check in the workflow.

### Triggers and publishing

- `pull_request` against `main`: all four cells build, verify, and
  test, uploading with `retention-days: 7`. No release. This is what
  keeps the static path from rotting between releases.
- `push` of a `v*` tag: the same four cells, then publish.
- `workflow_dispatch`: build on demand.

The publish job is gated on `needs: build` and
`if: startsWith(github.ref, 'refs/tags/v')`. It downloads all four
artifacts, generates `SHA256SUMS`, and creates the release with `gh`
rather than a third-party action: no new dependency to pin and audit,
and less for `zizmor.yml` to object to. `contents: write` is scoped to
that job alone; the workflow default remains `contents: read`.

Repository conventions apply throughout: actions pinned to a SHA with
a trailing version comment, `persist-credentials: false` on checkout,
a `concurrency` group, and `timeout-minutes` on every job.

## Documentation

- `docs/build.md`: a `-DFERRET_STATIC=ON|OFF` row in the CMake-knobs
  table, and a "Static builds" section covering the Linux-only
  constraint, the sanitizer mutual-exclusion, the Alpine recipe, and
  `nix build .#static`. The section names `release.yml` as the
  authoritative copy of the recipe.
- `README.md`: a "Prebuilt binaries" pointer to Releases. Downloading
  a binary is now the common path and belongs on the front page.
- `AGENTS.md`: `release.yml` added to the CI-gates table, plus a
  footgun entry: `FERRET_STATIC=ON` forces the FetchContent path, so a
  static configure requires network access even on a machine that has
  all four dependencies installed.
- `docs/android.md`: note that `ferret-android-arm64` is downloadable,
  making the cross-build section optional rather than mandatory.

No `scripts/build_static.sh`. The recipe lives in `release.yml` and is
quoted in `docs/build.md`, which points back at the workflow as
authoritative.

## Risks

**`nix flake check` may build `packages.static`.** If it does, every
PR pays the `pkgsStatic` build cost in `nix.yml`. Binary-cache
coverage for `x86_64-linux` `pkgsStatic` is expected to make this
tolerable, but the behaviour and the cost must be measured during
implementation rather than assumed. If it is slow, the response is to
raise `timeout-minutes`, not to hide the package from the check. An
unchecked flake output is the failure mode this design is trying to
avoid elsewhere.

**Default-PIE toolchains.** On distributions whose GCC specs force
`-pie`, `-static` can require an explicit `-no-pie` alongside it.
Alpine is not expected to need this; a contributor's host distribution
might.

**Recipe drift.** With no wrapper script, the Alpine recipe exists in
`release.yml` and is quoted in `docs/build.md`. The documentation
names the workflow as authoritative to contain this, but the two can
still fall out of sync.

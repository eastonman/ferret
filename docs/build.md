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
CMake will FetchContent CLI11, GoogleTest, spdlog, and sljit if they
aren't on the system search path. sljit is pinned to the same revision
as the Nix flake input — `CMakeLists.txt` reads `flake.lock` at
configure time so both build paths link identical sources.

```sh
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Android cross-build

Android uses the NDK CMake toolchain and a separate target build tree.
Do not reuse the canonical host `build/` tree for Android. See
[`android.md`](android.md) for the full cross-build and manual adb
execution workflow.

Scripts under `scripts/` need `numpy`, `pandas`, `plotly>=6.1.1`, and
`kaleido>=1.0` (pytest + ruff for tests). Nix users get these via
`flake.nix`; otherwise:

```sh
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt      # runtime
pip install -r requirements-dev.txt  # + pytest, ruff
```

Static-image plot exports (`.png`, `.svg`, `.pdf`, `.jpg`, `.webp`)
require Chrome or Chromium on `PATH` — kaleido runs a headless browser
to rasterize. The Nix dev shell ships `pkgs.chromium` on Linux. HTML
output has no such requirement.

## CMake knobs

| Option                            | Default | Purpose                                                                 |
|-----------------------------------|---------|-------------------------------------------------------------------------|
| `-DFERRET_WERROR=ON\|OFF`         | `ON`    | Treat warnings as errors on ferret's own targets (vendored deps unaffected). Turn off for one-off local builds against a newer compiler. CI runs with the default `ON`. |
| `-DFERRET_SANITIZER=<mode>`       | unset   | Enable a sanitizer build (see below).                                   |
| `-DCMAKE_BUILD_TYPE=<type>`       | unset   | Debug / Release / RelWithDebInfo. CI build job leaves this unset; sanitizer jobs set `Debug`. |
| `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` | off  | Required for `scripts/lint.sh` (clang-tidy reads `build/compile_commands.json`). |

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
cmake -S . -B build-asan -GNinja -DCMAKE_BUILD_TYPE=Debug -DFERRET_SANITIZER=address+undefined
cmake --build build-asan
ASAN_OPTIONS=halt_on_error=1:abort_on_error=0:exitcode=1:print_stacktrace=1 \
UBSAN_OPTIONS=halt_on_error=1:exitcode=1:print_stacktrace=1 \
  ctest --test-dir build-asan --output-on-failure
```

For TSan, swap the env block for:

```sh
TSAN_OPTIONS=halt_on_error=1:exitcode=1:second_deadlock_stack=1 \
  ctest --test-dir build-tsan --output-on-failure
```

The env vars match `.github/workflows/sanitizers.yml` verbatim — they
turn a sanitizer report into a non-zero exit. Notes:

- **Do not set `detect_leaks` explicitly on macOS.** Apple's ASan
  runtime aborts startup with "detect_leaks is not supported on this
  platform" if the key appears at all. On Linux, LSan runs under ASan
  by default without being named.
- **TSan and ASan cannot share a build tree** — they require separate
  compile-time instrumentation. Configure them into different
  `build-*` directories.

CI runs `address+undefined` and `thread` on Linux x86_64 and arm64,
and `address+undefined` only on macOS arm64 (TSan on Apple Clang is
patchy across Xcode versions). nixpkgs-clang ASan/TSan currently has
runtime-init issues on Apple Silicon — use UBSan there or build under
a Linux container.

## Running a single test

```sh
ctest --test-dir build -R test_timing --output-on-failure   # one binary
./build/tests/test_timing                                   # direct
./build/tests/test_timing --gtest_filter='Timing.TicksPerNsIsPositive'

./scripts/test_py.sh                                        # all Python (skips integration)
python3 -m pytest tests/python/test_surface.py              # one Python file
python3 -m pytest tests/python -m integration -v            # integration only (needs Chrome)
```

`ctest` only knows about C++ targets — Python tests are not
registered with it. The `integration` marker gates pytest cases that
spawn Chrome/kaleido; `scripts/test_py.sh` excludes them by passing
`-m "not integration"`.

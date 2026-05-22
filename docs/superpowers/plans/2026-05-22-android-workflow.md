# Android Workflow Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Document and validate a human-run Android build and execution workflow for ferret.

**Architecture:** Add a focused Android runbook and link it from the existing documentation entry points. Make the CMake build Android-cross-compile friendly by avoiding host binary/package assumptions and by deferring GoogleTest discovery when target binaries cannot run on the host.

**Tech Stack:** CMake, Ninja, Android NDK CMake toolchain, adb, GoogleTest, FetchContent, Markdown documentation.

---

## File Structure

- Create `docs/android.md`: natural-language Android build and adb runbook.
- Modify `README.md`: point users from the supported-platform table and documentation list to the Android workflow.
- Modify `docs/README.md`: add the Android workflow to the live documentation index.
- Modify `docs/build.md`: keep general host build docs here and link to the dedicated Android workflow.
- Modify `CMakeLists.txt`: avoid host dependency discovery for Android cross-builds, make Android ABI source selection explicit, and defer GoogleTest discovery while cross-compiling.

## Task 1: Add Android Workflow Runbook

**Files:**
- Create: `docs/android.md`

- [ ] **Step 1: Create `docs/android.md`**

Create the file with this exact content:

```markdown
# Android Workflow

Ferret can run as a native command-line binary on Android. Android is
not part of the normal pre-PR checklist and does not run in CI yet; this
page is a human-run workflow for cross-compiling, staging the binary on a
device with `adb`, running smoke checks, and pulling CSV results back to
the host for plotting.

Use this workflow when you want to validate ferret on a real phone such
as a Snapdragon 888 device.

## Prerequisites

Install these on the host:

- Android NDK.
- Android SDK platform-tools, providing `adb`.
- CMake and Ninja.
- Python dependencies from `requirements.txt` and `requirements-dev.txt`
  if you plan to run host-side plotting or Python tests.

Set `ANDROID_NDK_HOME` to the NDK directory:

```sh
export ANDROID_NDK_HOME="$HOME/Library/Android/sdk/ndk/26.3.11579264"
test -f "$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
```

The second command must exit zero. If it does not, adjust
`ANDROID_NDK_HOME` to the NDK version installed on your machine.

`adb` must see an authorized device:

```sh
adb devices
```

If the device is listed as `unauthorized`, unlock the phone and accept
the USB debugging prompt.

## Cross-Compile

Use a target build tree. Do not use the canonical host `build/`
directory; `build/` is reserved for local host development and
`scripts/lint.sh`.

```sh
cmake -S . -B build-android-arm64 -GNinja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-android-arm64
```

This builds the `ferret` binary plus the C++ test binaries for Android
ARM64. It does not run those binaries on the host.

Do not run host `ctest` against `build-android-arm64`. The files in that
tree are Android target binaries and must be run on an Android device or
under an Android emulator.

## Stage Binaries On The Device

Create a clean work directory under `/data/local/tmp` and push the main
binary plus a small set of smoke-test binaries:

```sh
adb shell 'rm -rf /data/local/tmp/ferret && mkdir -p /data/local/tmp/ferret'
adb push build-android-arm64/ferret /data/local/tmp/ferret/
adb push build-android-arm64/tests/test_smoke /data/local/tmp/ferret/
adb push build-android-arm64/tests/test_timing /data/local/tmp/ferret/
adb shell 'chmod +x /data/local/tmp/ferret/ferret /data/local/tmp/ferret/test_smoke /data/local/tmp/ferret/test_timing'
```

Run the smoke checks from the device work directory:

```sh
adb shell 'cd /data/local/tmp/ferret && ./ferret list'
adb shell 'cd /data/local/tmp/ferret && ./test_smoke'
adb shell 'cd /data/local/tmp/ferret && ./test_timing'
```

If a test fails, keep the full `adb shell` output. It usually contains
the exact missing permission, unsupported syscall, or timing assertion
that needs investigation.

## Run The Two-Step Cycle

Pick one core and use the same core for the frequency probe and the
target benchmark. The examples below use core 0 because every Android
device has it. For Snapdragon 888 investigations, you may want a big or
prime core instead; verify the device's CPU numbering before comparing
results across runs.

First run the frequency probe on the phone and pull the CSV back:

```sh
adb shell 'cd /data/local/tmp/ferret && ./ferret run dependent_chain_throughput --core=0 --out=freq.csv'
adb pull /data/local/tmp/ferret/freq.csv /tmp/ferret-android-freq.csv
python3 scripts/freq.py /tmp/ferret-android-freq.csv
```

Suppose the host-side frequency script reports `estimated_freq=2.4GHz`.
Use that value for the target benchmark:

```sh
adb shell 'cd /data/local/tmp/ferret && ./ferret run direct_branch_footprint --core=0 --branches=1..4096 --spacing_bytes=16..64 --freq=2.4GHz --out=btb.csv'
adb pull /data/local/tmp/ferret/btb.csv /tmp/ferret-android-btb.csv
```

Plot on the host:

```sh
python3 scripts/plot.py line /tmp/ferret-android-btb.csv --out=/tmp/ferret-android-btb.html
```

The phone runs the native benchmark and writes CSV. Plotting stays on
the host.

## Android Caveats

Android phones are noisy benchmark hosts. Keep the device cool, plugged
in, unlocked if needed, and as idle as practical. Disable background
work when possible.

Snapdragon 888 is heterogeneous: it has efficiency cores, performance
cores, and a prime core. Different cores can have different frontend
structures and different frequencies. Always record the core number and
keep the frequency probe and target benchmark on the same core.

Ferret's system controls are best-effort in Android user space.
`pin_to_core`, priority boost, and memory locking may fail on non-root
devices. Ferret should warn and continue; benchmark timing quality is
the operator's responsibility.

## Troubleshooting

- `adb devices` prints no device: check the USB cable, USB mode, and
  that platform-tools are on `PATH`.
- `adb devices` prints `unauthorized`: unlock the phone and accept the
  USB debugging prompt.
- CMake cannot find the Android toolchain: set `ANDROID_NDK_HOME` to a
  directory containing `build/cmake/android.toolchain.cmake`.
- CMake finds host libraries while cross-compiling: use a clean
  `build-android-arm64/` directory and keep Android builds separate from
  host builds.
- A binary prints `Permission denied`: rerun the `chmod +x` command for
  the staged files.
- A CSV is missing after a run: check that the `--out` path is inside
  `/data/local/tmp/ferret` or another writable device directory.
- Timing data looks unstable: rerun the frequency probe and benchmark on
  the same core after the device cools down.
```

- [ ] **Step 2: Check the new document for unfinished markers**

Run:

```sh
rg -n "TBD|TODO|FIXME" docs/android.md
```

Expected: no matches. `rg` exits with status 1 when it finds no matches.

- [ ] **Step 3: Commit the Android runbook**

Run:

```sh
git add docs/android.md
git commit -m "docs: add android workflow"
```

Expected: commit succeeds.

## Task 2: Link The Android Workflow From Existing Docs

**Files:**
- Modify: `README.md`
- Modify: `docs/README.md`
- Modify: `docs/build.md`

- [ ] **Step 1: Update the README build reference**

In `README.md`, replace this sentence:

```markdown
Full build options, sanitizer matrix, and non-Nix recipes: [`docs/build.md`](docs/build.md).
```

with:

```markdown
Full build options, sanitizer matrix, Android cross-build workflow, and non-Nix recipes:
[`docs/build.md`](docs/build.md), [`docs/android.md`](docs/android.md).
```

- [ ] **Step 2: Add the Android page to the README documentation list**

In `README.md`, add this bullet after the `docs/build.md` bullet:

```markdown
- [`docs/android.md`](docs/android.md) — Android NDK cross-build and manual adb execution workflow.
```

- [ ] **Step 3: Update the docs index**

In `docs/README.md`, add this bullet after the `build.md` bullet:

```markdown
- [`android.md`](android.md) — Android NDK cross-build and manual adb execution workflow.
```

- [ ] **Step 4: Add a short Android pointer to `docs/build.md`**

In `docs/build.md`, insert this section between the plain CMake example
and the paragraph that starts `Scripts under`:

```markdown
## Android cross-build

Android uses the NDK CMake toolchain and a separate target build tree.
Do not reuse the canonical host `build/` tree for Android. See
[`android.md`](android.md) for the full cross-build and manual adb
execution workflow.
```

- [ ] **Step 5: Verify links and key phrases**

Run:

```sh
rg -n "android.md|Android cross-build|manual adb|build-android-arm64" README.md docs/README.md docs/build.md docs/android.md
```

Expected: matches in all four files.

- [ ] **Step 6: Commit the doc links**

Run:

```sh
git add README.md docs/README.md docs/build.md
git commit -m "docs: link android workflow"
```

Expected: commit succeeds.

## Task 3: Avoid Host Dependencies In Android Cross-Builds

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Add the dependency-source policy**

In `CMakeLists.txt`, add this block after the `FERRET_WERROR` option:

```cmake
set(_ferret_use_system_deps ON)
if(CMAKE_SYSTEM_NAME STREQUAL "Android")
  set(_ferret_use_system_deps OFF)
endif()
```

- [ ] **Step 2: Guard host `find_package(GTest)`**

Replace:

```cmake
find_package(GTest QUIET)
if(NOT GTest_FOUND)
```

with:

```cmake
if(_ferret_use_system_deps)
  find_package(GTest QUIET)
endif()
if(NOT GTest_FOUND)
```

- [ ] **Step 3: Guard host `find_package(CLI11)`**

Replace:

```cmake
find_package(CLI11 QUIET)
if(NOT CLI11_FOUND)
```

with:

```cmake
if(_ferret_use_system_deps)
  find_package(CLI11 QUIET)
endif()
if(NOT CLI11_FOUND)
```

- [ ] **Step 4: Guard host `find_package(spdlog)`**

Replace:

```cmake
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
```

with:

```cmake
if(_ferret_use_system_deps)
  find_package(spdlog QUIET)
endif()
if(NOT spdlog_FOUND)
```

- [ ] **Step 5: Guard host sljit discovery**

Replace:

```cmake
find_path(SLJIT_INCLUDE_DIR sljitLir.h PATH_SUFFIXES sljit)
find_library(SLJIT_LIBRARY sljit)
if(SLJIT_INCLUDE_DIR AND SLJIT_LIBRARY)
```

with:

```cmake
if(_ferret_use_system_deps)
  find_path(SLJIT_INCLUDE_DIR sljitLir.h PATH_SUFFIXES sljit)
  find_library(SLJIT_LIBRARY sljit)
endif()
if(SLJIT_INCLUDE_DIR AND SLJIT_LIBRARY)
```

- [ ] **Step 6: Verify host configure still uses system packages when available**

Run:

```sh
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Expected: configure succeeds for the host build.

- [ ] **Step 7: Commit the dependency policy**

Run:

```sh
git add CMakeLists.txt
git commit -m "build: avoid host deps for android"
```

Expected: commit succeeds.

## Task 4: Make Android Source Selection And Test Discovery Cross-Build Safe

**Files:**
- Modify: `CMakeLists.txt`

- [ ] **Step 1: Make timing source selection Android ABI-aware**

Replace the timing source selection block:

```cmake
set(ferret_arch_timing_src "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
  set(ferret_arch_timing_src src/timing/x86_64.cpp)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64)")
  set(ferret_arch_timing_src src/timing/aarch64.cpp)
else()
  message(FATAL_ERROR "ferret: unsupported CMAKE_SYSTEM_PROCESSOR='${CMAKE_SYSTEM_PROCESSOR}' "
                      "(supported: x86_64, aarch64)")
endif()
```

with:

```cmake
set(ferret_arch_timing_src "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)" OR CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
  set(ferret_arch_timing_src src/timing/x86_64.cpp)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64)" OR CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
  set(ferret_arch_timing_src src/timing/aarch64.cpp)
else()
  message(FATAL_ERROR "ferret: unsupported target arch "
                      "CMAKE_SYSTEM_PROCESSOR='${CMAKE_SYSTEM_PROCESSOR}', "
                      "CMAKE_ANDROID_ARCH_ABI='${CMAKE_ANDROID_ARCH_ABI}' "
                      "(supported: x86_64, aarch64)")
endif()
```

- [ ] **Step 2: Make padding source selection Android ABI-aware**

Replace the padding source selection block:

```cmake
set(ferret_arch_padding_src "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
  set(ferret_arch_padding_src src/padding/x86_64.cpp)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64)")
  set(ferret_arch_padding_src src/padding/aarch64.cpp)
endif()
```

with:

```cmake
set(ferret_arch_padding_src "")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)" OR CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
  set(ferret_arch_padding_src src/padding/x86_64.cpp)
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(aarch64|arm64)" OR CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
  set(ferret_arch_padding_src src/padding/aarch64.cpp)
endif()
```

- [ ] **Step 3: Defer GoogleTest discovery while cross-compiling**

Replace the bottom of `CMakeLists.txt`:

```cmake
enable_testing()
include(GoogleTest)
add_subdirectory(tests)
```

with:

```cmake
if(CMAKE_CROSSCOMPILING)
  set(CMAKE_GTEST_DISCOVER_TESTS_DISCOVERY_MODE PRE_TEST)
endif()

enable_testing()
include(GoogleTest)
add_subdirectory(tests)
```

- [ ] **Step 4: Verify host configure still succeeds**

Run:

```sh
cmake -S . -B build -GNinja -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
```

Expected: configure succeeds.

- [ ] **Step 5: Commit the cross-build CMake fixes**

Run:

```sh
git add CMakeLists.txt
git commit -m "build: support android cross build"
```

Expected: commit succeeds.

## Task 5: Validate Host And Android Builds

**Files:**
- No planned file changes.

- [ ] **Step 1: Build the host tree**

Run:

```sh
cmake --build build
```

Expected: host build succeeds.

- [ ] **Step 2: Run host C++ tests**

Run:

```sh
ctest --test-dir build --output-on-failure
```

Expected: all host C++ tests pass, with any platform-specific skips already expected by the existing tests.

- [ ] **Step 3: Check the Android NDK toolchain**

Run:

```sh
test -f "$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake"
```

Expected: exit code 0. If it exits non-zero, stop and ask the user to install the Android NDK or set `ANDROID_NDK_HOME`; do not fake the Android validation.

- [ ] **Step 4: Configure the Android ARM64 build**

Run:

```sh
cmake -S . -B build-android-arm64 -GNinja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release
```

Expected: configure succeeds and reports Android as the target system.

- [ ] **Step 5: Build the Android ARM64 tree**

Run:

```sh
cmake --build build-android-arm64
```

Expected: Android target build succeeds. The build must not try to execute Android test binaries on the host.

- [ ] **Step 6: Optionally run an adb smoke check if a device is authorized**

Run:

```sh
adb devices
```

If the output contains one `device` entry, run:

```sh
adb shell 'rm -rf /data/local/tmp/ferret && mkdir -p /data/local/tmp/ferret'
adb push build-android-arm64/ferret /data/local/tmp/ferret/
adb shell 'chmod +x /data/local/tmp/ferret/ferret'
adb shell 'cd /data/local/tmp/ferret && ./ferret list'
```

Expected: `ferret list` prints the registered benchmark names. If there is no authorized device, record that adb execution was skipped and keep the build validation as the required Android evidence.

- [ ] **Step 7: Run lint**

Run:

```sh
./scripts/lint.sh
```

Expected: lint succeeds.

- [ ] **Step 8: Record validation in the final response**

Include the exact commands that passed. If Android validation stopped because the NDK was missing, state that the blocker was missing `ANDROID_NDK_HOME` or the NDK toolchain file.

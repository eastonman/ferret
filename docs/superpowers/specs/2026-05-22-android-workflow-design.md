# Android Build Workflow Design

## Goal

Add a human-run Android workflow for ferret. The workflow should make it
clear how to cross-compile the command-line binary and C++ test binaries
for an Android phone, move them to the device with `adb`, run a small
smoke check, and pull result files back to the host for plotting.

The immediate implementation goal is documentation plus local build
validation. Android execution remains a manual operator step, and
Android CI is deferred.

## Scope

In scope:

- Document required tools: Android NDK, `adb`, CMake, Ninja, and host
  Python dependencies for plotting.
- Document an Android `arm64-v8a` cross-build in a separate build tree,
  such as `build-android-arm64/`.
- Document the manual `adb` flow: confirm the device, create a work
  directory under `/data/local/tmp/ferret`, push `ferret` and selected
  test binaries, mark them executable, run smoke commands, and pull CSV
  output back to the host.
- Document how the normal two-step ferret workflow maps to Android:
  run `dependent_chain_throughput` on the phone to estimate the running
  core frequency, then run the target benchmark with `--freq` on the
  same core.
- Validate that the Android cross-build configures and builds locally
  when the NDK is available.
- Fix only Android build issues that block that cross-build.

Out of scope:

- GitHub Actions or other Android CI.
- An automated `adb` runner script.
- Requiring a connected phone for the normal contributor checklist.
- Automated assertions about benchmark output from Snapdragon 888 or any
  other device.

## Documentation Shape

Add a dedicated Android workflow page, `docs/android.md`, and link it
from the existing build and documentation entry points. The page should
be written as a natural-language runbook with exact commands included
where they remove ambiguity.

The page should cover:

1. Prerequisites and environment variables.
2. Cross-compilation with the NDK CMake toolchain.
3. Host-side build validation.
4. Manual `adb` staging and execution.
5. Pulling CSV files back to the host.
6. Host-side plotting.
7. Android-specific caveats.

The existing `docs/build.md` remains the general build reference. It
should point to `docs/android.md` rather than absorbing the full Android
flow.

## Cross-Build Flow

The documented build uses the NDK's CMake toolchain file and a separate
build directory:

```sh
cmake -S . -B build-android-arm64 -GNinja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-android-arm64
```

`android-26` is the default recommendation because it is old enough for
wide device coverage while still comfortably supporting modern 64-bit
Android phones. The doc can mention increasing the API level if a local
NDK or device requires it.

The build tree is intentionally not `build/`. The canonical `build/`
tree remains reserved for local host development and `lint.sh`.

## Device Flow

The manual device flow stages binaries under `/data/local/tmp/ferret`:

```sh
adb devices
adb shell mkdir -p /data/local/tmp/ferret
adb push build-android-arm64/ferret /data/local/tmp/ferret/
adb shell chmod +x /data/local/tmp/ferret/ferret
adb shell /data/local/tmp/ferret/ferret list
```

The doc should also show how to push selected test binaries from
`build-android-arm64/tests/` and run them directly. It should avoid
claiming that host `ctest` can execute Android target binaries.

For benchmark execution, the doc should show a small frequency probe and
benchmark run that writes CSV files into the same device work directory,
then pulls them back:

```sh
adb shell /data/local/tmp/ferret/ferret run dependent_chain_throughput \
  --core=0 --out=/data/local/tmp/ferret/freq.csv
adb pull /data/local/tmp/ferret/freq.csv /tmp/ferret-android-freq.csv
python3 scripts/freq.py /tmp/ferret-android-freq.csv
```

The target benchmark example should reuse the same `--core` and pass the
frequency reported by the host-side `scripts/freq.py` step.

## Platform Caveats

The Android page should call out these practical constraints:

- Snapdragon 888 is heterogeneous. The operator should keep the
  frequency probe and target benchmark on the same core, and should note
  which core was used.
- `pin_to_core`, priority boost, and memory locking are best-effort in
  user space. Non-root Android devices may deny some of these operations;
  ferret should warn and continue.
- Doze, thermal throttling, background apps, and vendor power management
  can distort timing. The doc should recommend a quiet device state and
  repeated runs when data looks suspicious.
- Plotting remains host-side. The Android device only runs the native
  binary and writes CSV output.

## Error Handling

The runbook should include short troubleshooting notes for common
failures:

- `adb devices` shows no device or an unauthorized device.
- `ANDROID_NDK_HOME` is unset or points at a directory without the CMake
  toolchain file.
- CMake cannot find Ninja or required host build tools.
- CMake or GoogleTest tries to execute target binaries on the host during
  cross-compilation.
- Android denies affinity, priority, or memory-lock operations.
- The device cannot write to the chosen output path.

## Validation

Implementation validation should include:

- Configure the Android `arm64-v8a` build tree when the NDK is available.
- Build the `ferret` binary and C++ test binaries.
- If a connected authorized device is available and the build succeeds,
  optionally run `ferret list` manually through `adb` as an extra smoke
  check. This is useful evidence, but it is not a CI requirement and
  should not be automated as part of this change.
- Run the normal host-side checks required by the touched files where
  practical, especially documentation spelling/format consistency and
  any C++ build checks needed for code fixes.

## Deferred Android CI

Android CI should be a later change. A future CI design can add an
NDK-only cross-compilation job that configures and builds `arm64-v8a`
without any device execution. Device execution should stay out of CI
unless a stable device farm or emulator strategy exists, because ferret's
useful validation depends on native execution behavior and timing noise.

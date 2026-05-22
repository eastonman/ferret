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

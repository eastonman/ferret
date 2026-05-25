# Android Workflow

Ferret can run as a native command-line binary on Android. Android is
not part of the normal pre-PR checklist and does not run in CI yet; this
page is a human-run workflow for cross-compiling, staging the binary on a
device with `adb`, running smoke checks, and pulling CSV results back to
the host for plotting.

Use this workflow when you want to validate ferret on a real phone —
for example a Snapdragon 888 or Snapdragon 8 Gen 3 device.

## Prerequisites

Enter the Android dev shell. It pulls in the NDK, platform-tools (`adb`),
CMake, and Ninja, and sets `ANDROID_NDK_HOME` for you:

```sh
NIXPKGS_ALLOW_UNFREE=1 nix develop --impure .#android
```

`adb` must see an authorized device:

```sh
adb devices
```

If the device is listed as `unauthorized`, unlock the phone and accept
the USB debugging prompt.

## Cross-Compile

Use a target build tree separate from the host `build/`. `scripts/lint.sh`
reads `build/compile_commands.json` to pick the files it lints, so the
host `build/` must keep host-compiled compile commands; do not let an
Android configure overwrite them.

```sh
cmake -S . -B build-android-arm64 -GNinja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-35 \
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
device has it. For frontend investigations you usually want a big or
prime core instead; see "Android CPU Numbering" below to pick one for
the SoC under test.

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

Modern Android SoCs are heterogeneous: each one mixes efficiency cores,
performance cores, and (typically) a single prime core. Different cores
have different frontend structures and different frequencies. Always
record the core number and keep the frequency probe and target benchmark
on the same core.

Ferret's system controls are best-effort in Android user space.
`pin_to_core`, priority boost, and memory locking may fail on non-root
devices. Ferret should warn and continue; benchmark timing quality is
the operator's responsibility.

## Android CPU Numbering

By Linux/Android convention, `/sys/devices/system/cpu/cpuN` is the
logical CPU number the kernel exposes, and Qualcomm SoCs in the
Snapdragon 8-series number cores from lowest-class (efficiency) to
highest-class (prime), in cluster order. CPU 0 is therefore the slowest
core on the chip and the highest-numbered CPU is the prime core. Pass
the number you want with `--core=N`.

Confirm the layout on the device under test before pinning:

```sh
adb shell 'cat /proc/cpuinfo | grep "CPU part"'
adb shell 'for c in /sys/devices/system/cpu/cpu*/cpufreq/cpuinfo_max_freq; do
  echo -n "$c "; cat $c
done'
```

`CPU part` values map to ARM core names (e.g. `0xd44 = Cortex-X1`,
`0xd82 = Cortex-X4`). For reference, two SoCs the runbook has been used
on:

| SoC | CPU 0–N | Cluster mapping |
|---|---|---|
| Snapdragon 888 (lahaina) | 0–3 / 4–6 / 7 | Cortex-A55 / Cortex-A78 / Cortex-X1 (prime) |
| Snapdragon 8 Gen 3 (pineapple / SM8650) | 0–1 / 2–4 / 5–6 / 7 | Cortex-A520 / Cortex-A720 (fast) / Cortex-A720 (slow) / Cortex-X4 (prime) |

On both, `--core=7` selects the prime core and `--core=0` selects the
slowest efficiency core. Other SoCs follow the same low-to-high
convention but cluster sizes vary, so always confirm with the commands
above.

## Troubleshooting

- `adb devices` prints no device: check the USB cable, USB mode, and
  that platform-tools are on `PATH`.
- `adb devices` prints `unauthorized`: unlock the phone and accept the
  USB debugging prompt.
- CMake cannot find the Android toolchain: enter the `.#android` dev
  shell so `ANDROID_NDK_HOME` is set, or point it at a directory
  containing `build/cmake/android.toolchain.cmake`.
- CMake finds host libraries while cross-compiling: use a clean
  `build-android-arm64/` directory and keep Android builds separate from
  host builds.
- A binary prints `Permission denied`: rerun the `chmod +x` command for
  the staged files.
- A CSV is missing after a run: check that the `--out` path is inside
  `/data/local/tmp/ferret` or another writable device directory.
- Timing data looks unstable: rerun the frequency probe and benchmark on
  the same core after the device cools down.

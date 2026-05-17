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
CMake will FetchContent CLI11, GoogleTest, and sljit if they aren't on
the system search path:

```sh
cmake -S . -B build -GNinja
cmake --build build
ctest --test-dir build --output-on-failure
```

Scripts under `scripts/` need matplotlib, numpy, and pandas (plus
pytest and ruff for tests). Nix users get these via `flake.nix`;
otherwise:

```sh
python3 -m venv .venv && source .venv/bin/activate
pip install -r requirements.txt      # runtime
pip install -r requirements-dev.txt  # + pytest, ruff
```

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
cmake -S . -B build-asan -GNinja -DFERRET_SANITIZER=address+undefined
cmake --build build-asan
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 \
  ctest --test-dir build-asan --output-on-failure
```

CI runs `address+undefined` and `thread` on Linux x86_64 and arm64
(`.github/workflows/sanitizers.yml`). macOS sanitizer support depends
on the toolchain; nixpkgs-clang ASan/TSan currently has runtime-init
issues on Apple Silicon — use UBSan there or build under a Linux
container.

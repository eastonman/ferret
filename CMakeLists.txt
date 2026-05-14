cmake_minimum_required(VERSION 3.20)
project(ferret LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# --- Warning policy ---
# Scoped to ferret's own targets via the ferret_warnings INTERFACE library;
# vendored deps (gtest/CLI11/spdlog/sljit) are deliberately excluded so a
# warning regression upstream cannot fail our build. FERRET_WERROR is ON
# by default so CI catches regressions without extra workflow plumbing;
# turn it OFF (-DFERRET_WERROR=OFF) for a one-off local build.
option(FERRET_WERROR "Treat warnings as errors for ferret targets" ON)

add_library(ferret_warnings INTERFACE)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
  target_compile_options(ferret_warnings INTERFACE
    -Wall -Wextra -Wpedantic
    $<$<BOOL:${FERRET_WERROR}>:-Werror>)
elseif(MSVC)
  target_compile_options(ferret_warnings INTERFACE
    /W4
    $<$<BOOL:${FERRET_WERROR}>:/WX>)
endif()

# --- Sanitizers ---
# Off by default — production and timing-sensitive runs are unchanged.
# Flags are added via add_compile_options / add_link_options before
# FetchContent so vendored deps (gtest, CLI11, spdlog, sljit) are
# instrumented too. Required: shadow memory in sanitizer-built code is
# not safe to mix with uninstrumented libs that allocate or take
# addresses of locals in ways ASan/UBSan would otherwise see as
# violations.
set(FERRET_SANITIZER "" CACHE STRING
  "Enable sanitizers. One of: address, undefined, address+undefined, thread.")
set_property(CACHE FERRET_SANITIZER PROPERTY STRINGS
  "" "address" "undefined" "address+undefined" "thread")

if(FERRET_SANITIZER)
  if(FERRET_SANITIZER STREQUAL "address")
    set(_ferret_san_arg "-fsanitize=address")
  elseif(FERRET_SANITIZER STREQUAL "undefined")
    set(_ferret_san_arg "-fsanitize=undefined")
  elseif(FERRET_SANITIZER STREQUAL "address+undefined")
    set(_ferret_san_arg "-fsanitize=address,undefined")
  elseif(FERRET_SANITIZER STREQUAL "thread")
    set(_ferret_san_arg "-fsanitize=thread")
  else()
    message(FATAL_ERROR
      "FERRET_SANITIZER='${FERRET_SANITIZER}' is invalid. "
      "Valid values: address, undefined, address+undefined, thread.")
  endif()
  message(STATUS "ferret: sanitizers enabled (${FERRET_SANITIZER})")
  # -fno-omit-frame-pointer and -g produce readable sanitizer stack
  # traces. Do not force an optimization level: the caller's build type
  # (Debug/RelWithDebInfo/Release) still applies.
  add_compile_options(${_ferret_san_arg} -fno-omit-frame-pointer -g)
  add_link_options(${_ferret_san_arg})
endif()

# --- GoogleTest: find_package first, FetchContent fallback ---
find_package(GTest QUIET)
if(NOT GTest_FOUND)
  include(FetchContent)
  FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  if(NOT TARGET GTest::gtest)
    add_library(GTest::gtest ALIAS gtest)
  endif()
  if(NOT TARGET GTest::gtest_main)
    add_library(GTest::gtest_main ALIAS gtest_main)
  endif()
endif()

# --- CLI11: find_package first, FetchContent fallback ---
find_package(CLI11 QUIET)
if(NOT CLI11_FOUND)
  include(FetchContent)
  FetchContent_Declare(CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.2
  )
  FetchContent_MakeAvailable(CLI11)
endif()

# --- spdlog: find_package first, FetchContent fallback ---
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
  include(FetchContent)
  FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.14.1
  )
  FetchContent_MakeAvailable(spdlog)
endif()

# --- sljit: find headers + library on the system path first, fall back
# to FetchContent of upstream. The Nix derivation installs into
# $out/include/sljit/sljitLir.h and $out/lib/libsljit.a.
find_path(SLJIT_INCLUDE_DIR sljitLir.h PATH_SUFFIXES sljit)
find_library(SLJIT_LIBRARY sljit)
if(SLJIT_INCLUDE_DIR AND SLJIT_LIBRARY)
  add_library(sljit::sljit UNKNOWN IMPORTED)
  set_target_properties(sljit::sljit PROPERTIES
    IMPORTED_LOCATION "${SLJIT_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${SLJIT_INCLUDE_DIR}"
  )
else()
  include(FetchContent)
  # SOURCE_SUBDIR set to a non-existent directory so MakeAvailable
  # populates the source tree without calling add_subdirectory —
  # sljit's own CMakeLists builds a test exe with Windows-only link
  # flags (-Wl,--stack,...) that break Linux/macOS builds.
  FetchContent_Declare(sljit_fetched
    GIT_REPOSITORY https://github.com/zherczeg/sljit.git
    GIT_TAG d9902b1ba06f477dcb89137dae8c8649fb58b7fb
    SOURCE_SUBDIR  do_not_use_upstream_cmake
  )
  FetchContent_MakeAvailable(sljit_fetched)
  # We compile sljit_src/sljitLir.c into a static library ourselves.
  add_library(sljit_static STATIC
    "${sljit_fetched_SOURCE_DIR}/sljit_src/sljitLir.c"
  )
  target_include_directories(sljit_static
    PUBLIC "${sljit_fetched_SOURCE_DIR}/sljit_src"
  )
  target_compile_options(sljit_static PRIVATE -O2 -fPIC)
  add_library(sljit::sljit ALIAS sljit_static)
endif()

add_library(ferret_core STATIC
  src/axis.cpp
  src/params.cpp
  src/sweep.cpp
  src/cli_axis.cpp
  src/freq.cpp
  src/jit.cpp
  src/timing/x86_64.cpp
  src/timing/aarch64.cpp
  src/timing/calibrate.cpp
  src/pinning/linux.cpp
  src/pinning/macos.cpp
  src/benchmark_registry.cpp
  src/csv.cpp
  src/runner.cpp
  src/run_command.cpp
  src/padding.cpp
  src/log.cpp
  src/permute.cpp
)
target_include_directories(ferret_core PUBLIC include)
target_link_libraries(ferret_core PUBLIC sljit::sljit spdlog::spdlog)
target_link_libraries(ferret_core PRIVATE ferret_warnings)
target_compile_features(ferret_core PUBLIC cxx_std_20)

add_executable(ferret
  src/main.cpp
  benchmarks/dependent_chain_throughput.cpp
  benchmarks/direct_branch_footprint.cpp
)
target_link_libraries(ferret PRIVATE
  ferret_core
  CLI11::CLI11
  sljit::sljit
  ferret_warnings
)

install(TARGETS ferret RUNTIME DESTINATION bin)

enable_testing()
include(GoogleTest)
add_subdirectory(tests)

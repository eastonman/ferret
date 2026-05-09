cmake_minimum_required(VERSION 3.20)
project(ferret LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
  FetchContent_Declare(sljit_fetched
    GIT_REPOSITORY https://github.com/zherczeg/sljit.git
    GIT_TAG d9902b1ba06f477dcb89137dae8c8649fb58b7fb
  )
  FetchContent_MakeAvailable(sljit_fetched)
  # sljit's CMakeLists.txt only builds a test exe and uses GNU-ld flags
  # that fail on macOS, so we don't use it. Instead, we compile
  # sljit_src/sljitLir.c into a static library ourselves.
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
  src/sweep.cpp
  src/cli_axis.cpp
  src/timing/x86_64.cpp
  src/timing/aarch64.cpp
  src/timing/calibrate.cpp
  src/pinning/linux.cpp
  src/pinning/macos.cpp
  src/benchmark_registry.cpp
  src/output/csv.cpp
  src/runner.cpp
  src/padding.cpp
)
target_include_directories(ferret_core PUBLIC include)
target_link_libraries(ferret_core PUBLIC sljit::sljit)
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
)

enable_testing()
include(GoogleTest)
add_subdirectory(tests)

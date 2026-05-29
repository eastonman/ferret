add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE ferret_core GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_smoke)

add_executable(test_deps_link test_deps_link.cpp)
target_link_libraries(test_deps_link PRIVATE GTest::gtest GTest::gtest_main CLI11::CLI11 sljit::sljit)
gtest_discover_tests(test_deps_link)

add_executable(test_params_axis test_params_axis.cpp)
target_link_libraries(test_params_axis PRIVATE ferret_core GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_params_axis)

add_executable(test_sweep test_sweep.cpp)
target_link_libraries(test_sweep PRIVATE ferret_core GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_sweep)

add_executable(test_cli_axis test_cli_axis.cpp)
target_link_libraries(test_cli_axis PRIVATE ferret_core GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_cli_axis)

add_executable(test_timing test_timing.cpp)
target_link_libraries(test_timing PRIVATE ferret_core GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_timing)

add_executable(test_pinning test_pinning.cpp)
target_link_libraries(test_pinning PRIVATE ferret_core GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_pinning)

add_executable(test_benchmark_registry test_benchmark_registry.cpp)
target_link_libraries(
  test_benchmark_registry PRIVATE ferret_core sljit::sljit GTest::gtest GTest::gtest_main ferret_warnings
)
gtest_discover_tests(test_benchmark_registry)

add_executable(test_csv test_csv.cpp)
target_link_libraries(test_csv PRIVATE ferret_core GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_csv)

add_executable(test_runner test_runner.cpp)
target_link_libraries(test_runner PRIVATE ferret_core GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_runner)

add_executable(test_padding test_padding.cpp)
target_link_libraries(test_padding PRIVATE ferret_core sljit::sljit GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_padding)

add_executable(test_permute test_permute.cpp)
target_link_libraries(test_permute PRIVATE ferret_core GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_permute)

add_executable(test_bench_helpers test_bench_helpers.cpp)
target_link_libraries(
  test_bench_helpers PRIVATE ferret_core sljit::sljit GTest::gtest GTest::gtest_main ferret_warnings
)
gtest_discover_tests(test_bench_helpers)

add_executable(test_nested_call_depth test_nested_call_depth.cpp)
target_sources(test_nested_call_depth PRIVATE $<TARGET_OBJECTS:ferret_benchmarks>)
target_link_libraries(
  test_nested_call_depth PRIVATE ferret_core ferret_benchmarks sljit::sljit GTest::gtest GTest::gtest_main
                                 ferret_warnings
)
gtest_discover_tests(test_nested_call_depth)

add_executable(test_branch_history_footprint test_branch_history_footprint.cpp)
target_sources(test_branch_history_footprint PRIVATE $<TARGET_OBJECTS:ferret_benchmarks>)
target_link_libraries(
  test_branch_history_footprint PRIVATE ferret_core ferret_benchmarks sljit::sljit GTest::gtest GTest::gtest_main
                                        ferret_warnings
)
gtest_discover_tests(test_branch_history_footprint)

add_executable(test_direct_branch_footprint test_direct_branch_footprint.cpp)
target_sources(test_direct_branch_footprint PRIVATE $<TARGET_OBJECTS:ferret_benchmarks>)
target_link_libraries(
  test_direct_branch_footprint PRIVATE ferret_core ferret_benchmarks sljit::sljit GTest::gtest GTest::gtest_main
                                       ferret_warnings
)
gtest_discover_tests(test_direct_branch_footprint)

add_executable(test_jit test_jit.cpp)
target_link_libraries(test_jit PRIVATE ferret_core sljit::sljit GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_jit)

add_executable(test_freq test_freq.cpp)
target_link_libraries(test_freq PRIVATE ferret_core GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_freq)

add_executable(test_parse test_parse.cpp)
target_link_libraries(test_parse PRIVATE ferret_core GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_parse)

add_executable(test_run_options test_run_options.cpp)
target_link_libraries(test_run_options PRIVATE ferret_core GTest::gtest GTest::gtest_main ferret_warnings)
gtest_discover_tests(test_run_options)

add_executable(test_integration test_integration.cpp)
target_compile_definitions(test_integration PRIVATE FERRET_BINARY="$<TARGET_FILE:ferret>")
add_dependencies(test_integration ferret)
target_link_libraries(test_integration PRIVATE GTest::gtest GTest::gtest_main ferret_warnings)
# Per-test wall-clock timeout enforced by CTest itself (portable across
# Linux/macOS/BSD without depending on a `timeout` binary). 30 s is much
# longer than the longest legitimate integration test (~0.5 s for the
# valid sweeps); a regression that re-introduces a hang would manifest
# as a CTest TIMEOUT on the relevant entry rather than a wedged process.
gtest_discover_tests(
  test_integration
  PROPERTIES
  TIMEOUT 30
)

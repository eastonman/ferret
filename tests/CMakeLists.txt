add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE GTest::gtest GTest::gtest_main)
gtest_discover_tests(test_smoke)

add_executable(test_deps_link test_deps_link.cpp)
target_link_libraries(test_deps_link PRIVATE
  GTest::gtest GTest::gtest_main
  CLI11::CLI11
  sljit::sljit
)
gtest_discover_tests(test_deps_link)

add_executable(test_params_axis test_params_axis.cpp)
target_link_libraries(test_params_axis PRIVATE
  ferret_core
  GTest::gtest GTest::gtest_main
)
gtest_discover_tests(test_params_axis)

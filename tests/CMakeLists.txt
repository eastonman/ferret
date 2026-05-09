add_executable(test_smoke test_smoke.cpp)
target_link_libraries(test_smoke PRIVATE GTest::gtest GTest::gtest_main)
gtest_discover_tests(test_smoke)

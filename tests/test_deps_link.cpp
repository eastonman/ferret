#include <gtest/gtest.h>

#include <string>
#include <vector>

extern "C" {
#include <sljitLir.h>
}
#include <CLI/CLI.hpp>

TEST(DepsLink, SljitPlatformNameNotEmpty) {
  const char* name = sljit_get_platform_name();
  ASSERT_NE(name, nullptr);
  EXPECT_GT(std::string(name).size(), 0u);
}

TEST(DepsLink, Cli11ParsesEmptyArgs) {
  CLI::App app{"smoke"};
  std::vector<std::string> argv = {"prog"};
  std::vector<char*> raw;
  for (auto& s : argv) raw.push_back(s.data());
  EXPECT_NO_THROW(app.parse(static_cast<int>(raw.size()), raw.data()));
}

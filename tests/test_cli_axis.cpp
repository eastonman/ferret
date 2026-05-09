#include <gtest/gtest.h>

#include "ferret/axis.hpp"
#include "ferret/cli_axis.hpp"

using namespace ferret;

TEST(CliAxis, SingleValueProducesOneEntry) {
  Axis spacing = Axis::values("spacing", {16, 32, 64, 128});
  auto v = parse_cli_axis_value("64", spacing);
  EXPECT_EQ(v, (std::vector<int64_t>{64}));
}

TEST(CliAxis, CommaSeparatedValues) {
  Axis spacing = Axis::values("spacing", {16, 32, 64, 128});
  auto v = parse_cli_axis_value("16,64,128", spacing);
  EXPECT_EQ(v, (std::vector<int64_t>{16, 64, 128}));
}

TEST(CliAxis, RangeUsesAxisDeclaredStepPolicyLinear) {
  Axis x = Axis::range("x", 0, 100);
  auto v = parse_cli_axis_value("3..7", x);
  EXPECT_EQ(v, (std::vector<int64_t>{3, 4, 5, 6, 7}));
}

TEST(CliAxis, RangeUsesAxisDeclaredStepPolicyLog2) {
  Axis branches = Axis::log2_range("branches", 1, 1 << 15);
  auto v = parse_cli_axis_value("4..32", branches);
  EXPECT_EQ(v, (std::vector<int64_t>{4, 8, 16, 32}));
}

TEST(CliAxis, MalformedThrows) {
  Axis x = Axis::range("x", 0, 100);
  EXPECT_THROW((void)parse_cli_axis_value("nonsense", x), std::invalid_argument);
  EXPECT_THROW((void)parse_cli_axis_value("1..", x), std::invalid_argument);
  EXPECT_THROW((void)parse_cli_axis_value("..5", x), std::invalid_argument);
  EXPECT_THROW((void)parse_cli_axis_value("5..3", x), std::invalid_argument);
}

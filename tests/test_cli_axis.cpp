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

TEST(CliAxis, Log2RangeRejectsZeroLo) {
  Axis branches = Axis::log2_range("branches", 1, 1 << 15);
  // 0..N on a log2 axis is meaningless (multiplication never progresses)
  EXPECT_THROW((void)parse_cli_axis_value("0..8", branches), std::invalid_argument);
}

TEST(CliAxis, Log2RangeRejectsNegativeLo) {
  Axis branches = Axis::log2_range("branches", 1, 1 << 15);
  EXPECT_THROW((void)parse_cli_axis_value("-1..8", branches), std::invalid_argument);
}

TEST(CliAxis, Log2SingleValueRejectsZero) {
  Axis branches = Axis::log2_range("branches", 1, 1 << 15);
  EXPECT_THROW((void)parse_cli_axis_value("0", branches), std::invalid_argument);
}

TEST(CliAxis, Log2SingleValueRejectsNegative) {
  Axis branches = Axis::log2_range("branches", 1, 1 << 15);
  EXPECT_THROW((void)parse_cli_axis_value("-1", branches), std::invalid_argument);
}

TEST(CliAxis, Log2ListRejectsAnyNonPositive) {
  Axis branches = Axis::log2_range("branches", 1, 1 << 15);
  EXPECT_THROW((void)parse_cli_axis_value("1,2,0,4", branches), std::invalid_argument);
  EXPECT_THROW((void)parse_cli_axis_value("4,-2", branches), std::invalid_argument);
}

TEST(CliAxis, RangeAxisAcceptsZeroAndNegativeLists) {
  // Non-log2 ranges have no positivity requirement.
  Axis x = Axis::range("x", -10, 10);
  auto v = parse_cli_axis_value("-3,0,5", x);
  EXPECT_EQ(v, (std::vector<int64_t>{-3, 0, 5}));
}

// ----- parse_option_value -----

TEST(ParseOptionValue, ParsesPositiveInteger) { EXPECT_EQ(parse_option_value("42"), 42); }

TEST(ParseOptionValue, ParsesNegativeInteger) { EXPECT_EQ(parse_option_value("-7"), -7); }

TEST(ParseOptionValue, ParsesZero) { EXPECT_EQ(parse_option_value("0"), 0); }

TEST(ParseOptionValue, EmptyThrows) { EXPECT_THROW((void)parse_option_value(""), std::invalid_argument); }

TEST(ParseOptionValue, NonNumericThrows) { EXPECT_THROW((void)parse_option_value("abc"), std::invalid_argument); }

TEST(ParseOptionValue, TrailingJunkThrows) {
  EXPECT_THROW((void)parse_option_value("42x"), std::invalid_argument);
  EXPECT_THROW((void)parse_option_value("42.5"), std::invalid_argument);
}

// ----- parse_extras -----

TEST(ParseExtras, EmptyInputIsEmptyMap) { EXPECT_TRUE(parse_extras({}).empty()); }

TEST(ParseExtras, WellFormedTokens) {
  auto m = parse_extras({"--branches=1..8", "--spacing_bytes=64"});
  ASSERT_EQ(m.size(), 2U);
  EXPECT_EQ(m["branches"], "1..8");
  EXPECT_EQ(m["spacing_bytes"], "64");
}

TEST(ParseExtras, ValueMayContainEqualSign) {
  // First `=` separates name from value; rest of token is part of value.
  auto m = parse_extras({"--key=lo=hi"});
  ASSERT_EQ(m.size(), 1U);
  EXPECT_EQ(m["key"], "lo=hi");
}

TEST(ParseExtras, EmptyValueIsAllowed) {
  auto m = parse_extras({"--name="});
  ASSERT_EQ(m.size(), 1U);
  EXPECT_EQ(m["name"], "");
}

TEST(ParseExtras, MissingDoubleDashThrows) {
  EXPECT_THROW((void)parse_extras({"branches=1..8"}), std::invalid_argument);
  EXPECT_THROW((void)parse_extras({"-x=1"}), std::invalid_argument);
}

TEST(ParseExtras, MissingEqualsThrows) { EXPECT_THROW((void)parse_extras({"--branches"}), std::invalid_argument); }

TEST(ParseExtras, ShortTokenThrows) {
  // tok.size() < 3 → "--" alone or "-x" both fail.
  EXPECT_THROW((void)parse_extras({"--"}), std::invalid_argument);
}

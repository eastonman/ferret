#include <gtest/gtest.h>

#include <string>

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

TEST(CliAxis, GeomRangeWithoutSuffixUsesAxisK) {
  // Picked so the result obviously differs from the linear-range
  // fallback (which would be 101 elements for "100..200"); guards
  // against a regression where parse_cli_axis_value forgets the
  // GeomRange case and falls through.
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 4);
  auto v = parse_cli_axis_value("100..200", branches);
  EXPECT_EQ(v, (std::vector<int64_t>{100, 119, 141, 168, 200}));
}

TEST(CliAxis, GeomRangeAtSuffixOverridesAxisK) {
  // Axis declares k=4 but CLI passes @1 — CLI wins.
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 4);
  auto v = parse_cli_axis_value("1..8@1", branches);
  EXPECT_EQ(v, (std::vector<int64_t>{1, 2, 4, 8}));
}

TEST(CliAxis, GeomRangeAtSuffixDensifies) {
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 1);
  auto v = parse_cli_axis_value("1024..2048@4", branches);
  EXPECT_EQ(v, (std::vector<int64_t>{1024, 1218, 1448, 1722, 2048}));
}

TEST(CliAxis, GeomRangeAtSuffixOnLog2AxisThrowsWithSpecificMessage) {
  // Without @k handling, parse_int("32768@4") happens to throw too —
  // but with the wrong message. Asserting on the message text pins
  // that the rejection comes from the @k validator, not a coincidence.
  Axis branches = Axis::log2_range("branches", 1, 1 << 15);
  try {
    (void)parse_cli_axis_value("1..32768@4", branches);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    std::string what(e.what());
    EXPECT_NE(what.find("only valid for geom_range"), std::string::npos) << "got: " << what;
  }
}

TEST(CliAxis, GeomRangeAtSuffixOnLinearRangeAxisThrowsWithSpecificMessage) {
  Axis x = Axis::range("x", 0, 100);
  try {
    (void)parse_cli_axis_value("1..10@2", x);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    std::string what(e.what());
    EXPECT_NE(what.find("only valid for geom_range"), std::string::npos) << "got: " << what;
  }
}

TEST(CliAxis, GeomRangeAtSuffixRejectsNonPositiveK) {
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 1);
  EXPECT_THROW((void)parse_cli_axis_value("1..8@0", branches), std::invalid_argument);
  EXPECT_THROW((void)parse_cli_axis_value("1..8@-1", branches), std::invalid_argument);
}

TEST(CliAxis, GeomRangeAtSuffixRejectsMalformedK) {
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 1);
  EXPECT_THROW((void)parse_cli_axis_value("1..8@abc", branches), std::invalid_argument);
  EXPECT_THROW((void)parse_cli_axis_value("1..8@", branches), std::invalid_argument);
  EXPECT_THROW((void)parse_cli_axis_value("1..8@4x", branches), std::invalid_argument);
}

TEST(CliAxis, GeomRangeAtSuffixRejectsEmptyHi) {
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 1);
  EXPECT_THROW((void)parse_cli_axis_value("1..@4", branches), std::invalid_argument);
}

TEST(CliAxis, GeomRangeListSyntaxStillWorks) {
  Axis branches = Axis::geom_range("branches", 1, 1 << 15, 1);
  auto v = parse_cli_axis_value("100,250,500", branches);
  EXPECT_EQ(v, (std::vector<int64_t>{100, 250, 500}));
}

TEST(CliAxis, EmptyHiWithAtSuffixGivesMalformedRangeOnLog2) {
  // Empty hi (`1..@4`) reports the tokenization failure ("malformed
  // range"), not "@k only valid for geom_range" — the missing hi is
  // the real fault and its diagnostic must win.
  Axis branches = Axis::log2_range("branches", 1, 1 << 15);
  try {
    (void)parse_cli_axis_value("1..@4", branches);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    std::string what(e.what());
    EXPECT_NE(what.find("malformed range"), std::string::npos) << "got: " << what;
  }
}

TEST(CliAxis, EmptyHiWithAtSuffixGivesMalformedRangeOnLinearRange) {
  Axis x = Axis::range("x", 0, 100);
  try {
    (void)parse_cli_axis_value("1..@4", x);
    FAIL() << "expected std::invalid_argument";
  } catch (const std::invalid_argument& e) {
    std::string what(e.what());
    EXPECT_NE(what.find("malformed range"), std::string::npos) << "got: " << what;
  }
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

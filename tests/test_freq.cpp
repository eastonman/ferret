#include <gtest/gtest.h>

#include <cmath>
#include <stdexcept>

#include "ferret/freq.hpp"

using namespace ferret;

TEST(ParseFreq, GHzSuffix) { EXPECT_DOUBLE_EQ(parse_freq("4.521GHz"), 4.521e9); }

TEST(ParseFreq, MHzSuffix) { EXPECT_DOUBLE_EQ(parse_freq("100MHz"), 100e6); }

TEST(ParseFreq, KHzSuffix) { EXPECT_DOUBLE_EQ(parse_freq("250kHz"), 250e3); }

TEST(ParseFreq, HzSuffix) { EXPECT_DOUBLE_EQ(parse_freq("42Hz"), 42.0); }

TEST(ParseFreq, BareNumberIsHz) { EXPECT_DOUBLE_EQ(parse_freq("12345"), 12345.0); }

TEST(ParseFreq, ScientificNotation) {
  EXPECT_DOUBLE_EQ(parse_freq("1.2e9Hz"), 1.2e9);
  EXPECT_DOUBLE_EQ(parse_freq("1.2e9"), 1.2e9);
}

TEST(ParseFreq, EmptyStringThrows) { EXPECT_THROW((void)parse_freq(""), std::invalid_argument); }

TEST(ParseFreq, OnlySuffixThrows) { EXPECT_THROW((void)parse_freq("GHz"), std::invalid_argument); }

TEST(ParseFreq, TrailingJunkThrows) {
  EXPECT_THROW((void)parse_freq("4.5GHzx"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("4.5x"), std::invalid_argument);
}

TEST(ParseFreq, NonNumericThrows) {
  EXPECT_THROW((void)parse_freq("bogus"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("bogusGHz"), std::invalid_argument);
}

TEST(ParseFreq, InfThrows) {
  EXPECT_THROW((void)parse_freq("inf"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("infHz"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("-inf"), std::invalid_argument);
}

TEST(ParseFreq, NanThrows) {
  EXPECT_THROW((void)parse_freq("nan"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("nanGHz"), std::invalid_argument);
}

TEST(ParseFreq, ZeroThrows) {
  EXPECT_THROW((void)parse_freq("0"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("0GHz"), std::invalid_argument);
}

TEST(ParseFreq, NegativeThrows) {
  EXPECT_THROW((void)parse_freq("-4.5GHz"), std::invalid_argument);
  EXPECT_THROW((void)parse_freq("-1"), std::invalid_argument);
}

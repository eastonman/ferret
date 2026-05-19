#include <gtest/gtest.h>

#include "ferret/parse.hpp"

namespace {

TEST(Parse, ParsesPositiveInteger) {
  EXPECT_EQ(ferret::parse_int("42"), 42);
  EXPECT_EQ(ferret::parse_int("0"), 0);
  EXPECT_EQ(ferret::parse_int("-7"), -7);
  EXPECT_EQ(ferret::parse_int("9223372036854775807"), 9223372036854775807LL);
}

TEST(Parse, RejectsEmpty) { EXPECT_THROW(ferret::parse_int(""), std::invalid_argument); }

TEST(Parse, RejectsTrailingJunk) { EXPECT_THROW(ferret::parse_int("42abc"), std::invalid_argument); }

TEST(Parse, RejectsNonNumeric) { EXPECT_THROW(ferret::parse_int("abc"), std::invalid_argument); }

}  // namespace

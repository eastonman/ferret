#include <gtest/gtest.h>

#include "ferret/params.hpp"

using namespace ferret;

TEST(Smoke, ParamsRoundTrip) {
  Params p;
  p.set("branches", 1024);
  EXPECT_EQ(p.get<int64_t>("branches"), 1024);
}

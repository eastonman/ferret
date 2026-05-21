#include <gtest/gtest.h>

#include "ferret/run_command.hpp"

TEST(RunOptions, DefaultsDocumented) {
  ferret::RunOptions o{};
  EXPECT_EQ(o.reps, 7);
  EXPECT_EQ(o.warmup, 1);
  EXPECT_EQ(o.seed, 1);
}

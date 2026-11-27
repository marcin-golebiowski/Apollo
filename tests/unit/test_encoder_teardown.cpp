/**
 * @file tests/unit/test_encoder_teardown.cpp
 * @brief Verify the pending-teardown count and age reported for detached encoder teardowns.
 */
#include <gtest/gtest.h>

#include "src/encoder_teardown.h"

namespace {
  using namespace std::chrono_literals;
  using teardowns_t = video::encoder_teardowns_t;
  const teardowns_t::clock::time_point start {};

  TEST(EncoderTeardownTest, ReportsPendingCountAndOldestTeardown) {
    teardowns_t teardowns;
    EXPECT_EQ(teardowns.status(start).pending, 0u);
    EXPECT_EQ(teardowns.begin(start + 1s), 1u);
    EXPECT_EQ(teardowns.begin(start + 5s), 2u);
    auto status = teardowns.status(start + 31s);
    EXPECT_EQ(status.pending, 2u);
    EXPECT_EQ(status.oldest, 30s);
    EXPECT_EQ(teardowns.end(start + 1s), 1u);
    EXPECT_EQ(teardowns.status(start + 31s).oldest, 26s);
    EXPECT_EQ(teardowns.end(start + 5s), 0u);
    status = teardowns.status(start + 31s);
    EXPECT_EQ(status.pending, 0u);
    EXPECT_EQ(status.oldest, 0s);
  }

  TEST(EncoderTeardownTest, TeardownsStartedTogetherAreCountedSeparately) {
    teardowns_t teardowns;
    teardowns.begin(start);
    EXPECT_EQ(teardowns.begin(start), 2u);
    EXPECT_EQ(teardowns.end(start), 1u);
    EXPECT_EQ(teardowns.status(start + 2s).oldest, 2s);
    EXPECT_EQ(teardowns.end(start), 0u);
  }
}  // namespace

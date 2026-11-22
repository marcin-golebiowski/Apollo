/**
 * @file tests/unit/test_encoder_recovery.cpp
 * @brief Verify per-stream retry limits for the asynchronous encoder.
 */
#include <gtest/gtest.h>

#include "src/encoder_recovery.h"

namespace {
  using namespace std::chrono_literals;
  constexpr video::encode_run_result_t no_output {.failed = true};

  TEST(EncoderRecoveryTest, PersistentFailureExhaustsThreeIncreasingDelays) {
    video::encoder_recovery_t recovery;
    EXPECT_EQ(recovery.retry_delay(no_output), 100ms);
    EXPECT_EQ(recovery.retry_delay(no_output), 250ms);
    EXPECT_EQ(recovery.retry_delay(no_output), 500ms);
    EXPECT_FALSE(recovery.retry_delay(no_output));
    EXPECT_FALSE(recovery.retry_delay(no_output));
  }

  TEST(EncoderRecoveryTest, DeliveredVideoRestoresRetryBudget) {
    video::encoder_recovery_t recovery;
    for (int i = 0; i < 3; ++i) {
      ASSERT_TRUE(recovery.retry_delay(no_output));
    }
    EXPECT_EQ(recovery.retry_delay({.failed = true, .produced_packet = true}), 100ms);
    EXPECT_EQ(recovery.retry_delay(no_output), 250ms);
  }

  TEST(EncoderRecoveryTest, DisplayReinitializationDoesNotUseAnEncoderRetry) {
    video::encoder_recovery_t recovery;
    EXPECT_EQ(recovery.retry_delay(no_output), 100ms);
    EXPECT_EQ(recovery.retry_delay({}), 0ms);
    EXPECT_EQ(recovery.retry_delay(no_output), 250ms);
    EXPECT_EQ(recovery.retry_delay({.produced_packet = true}), 0ms);
    EXPECT_EQ(recovery.retry_delay(no_output), 100ms);
  }
}  // namespace

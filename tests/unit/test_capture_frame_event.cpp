/**
 * @file tests/unit/test_capture_frame_event.cpp
 * @brief Exercise the single pending frame shared by capture and the encoder during display reinitialization.
 */
#include <future>
#include <gtest/gtest.h>

#include "src/thread_safe.h"

namespace {
  using namespace std::chrono_literals;
  using frame_event_t = safe::event_t<std::shared_ptr<int>>;

  TEST(CaptureFrameEvent, ReinitFinishesWhenEncoderAlreadyTookLastFrame) {
    frame_event_t images;
    auto frame = std::make_shared<int>(42);
    images.raise(frame);
    ASSERT_EQ(images.pop(0ms), frame);

    auto discard = std::async(std::launch::async, [&] { return images.try_pop(); });
    auto result = discard.wait_for(2s);
    images.stop();  // Wake a regressed blocking implementation so the test can report failure.
    ASSERT_EQ(result, std::future_status::ready);
    EXPECT_EQ(discard.get(), nullptr);
  }

  TEST(CaptureFrameEvent, ReinitReleasesPendingFrameAndAcceptsTheNextFrame) {
    frame_event_t images;
    auto old_frame = std::make_shared<int>(1);
    std::weak_ptr<int> old_frame_ref = old_frame;
    images.raise(std::move(old_frame));
    images.try_pop();
    EXPECT_TRUE(old_frame_ref.expired());

    auto next_frame = std::make_shared<int>(2);
    images.raise(next_frame);
    EXPECT_EQ(images.pop(0ms), next_frame);
  }
}  // namespace

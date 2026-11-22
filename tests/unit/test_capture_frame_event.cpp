/**
 * @file tests/unit/test_capture_frame_event.cpp
 * @brief Exercise the single pending frame shared by capture and the encoder during display reinitialization.
 */
#include <future>
#include <gtest/gtest.h>

#include "src/display_recovery.h"
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

  TEST(DisplayRecovery, ImmediateResetsKeepTheDeadlineUntilCaptureResumes) {
    video::display_recovery_t recovery;
    const auto start = video::display_recovery_t::clock::now();
    const auto first_deadline = recovery.begin();
    EXPECT_GE(first_deadline, start + 10s);
    EXPECT_LE(first_deadline, video::display_recovery_t::clock::now() + 10s);
    EXPECT_EQ(recovery.begin(), first_deadline);
    recovery.capture_resumed();
    const auto resumed = video::display_recovery_t::clock::now();
    EXPECT_GE(recovery.begin(), resumed + 10s);
  }

  TEST(CaptureSessionQueue, FullOrStoppedQueueRejectsSessionsWithoutDroppingAcceptedWork) {
    safe::queue_t<int> sessions {1};
    ASSERT_TRUE(sessions.try_raise(1));
    EXPECT_FALSE(sessions.try_raise(2));
    EXPECT_EQ(sessions.pop(0ms), 1);

    ASSERT_TRUE(sessions.try_raise(3));
    sessions.stop();
    EXPECT_FALSE(sessions.try_raise(4));
    // The stopping capture thread must still be able to notify every accepted session.
    EXPECT_EQ(sessions.unsafe(), std::vector<int> {3});
  }
}  // namespace

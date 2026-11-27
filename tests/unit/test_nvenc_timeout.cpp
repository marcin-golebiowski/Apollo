/**
 * @file tests/unit/test_nvenc_timeout.cpp
 * @brief Exercise NVENC frame completion without a GPU or real-time sleeps.
 */
#include <gtest/gtest.h>

#include "src/nvenc/nvenc_base.h"

namespace {
  using Calls = std::vector<std::string>;

  class FakeEncoder: public nvenc::nvenc_base {
  public:
    FakeEncoder():
        nvenc_base(NV_ENC_DEVICE_TYPE_DIRECTX) {
      device = this;
      async_event_handle = &event;
      flush_event_handle = &flush_event;
    }

    ~FakeEncoder() override {
      destroy_encoder();
    }

    bool open() {
      video::config_t stream {};
      stream.width = 1280;
      stream.height = 720;
      stream.videoFormat = 1;  // HEVC
      stream.framerate = 60;
      stream.bitrate = 10000;
      return create_encoder({}, stream, {}, NV_ENC_BUFFER_FORMAT_NV12);
    }

    using nvenc_base::retirement_timeout;

    Calls calls;
    uint32_t ready_after_ms = 0;
    uint32_t flush_ready_after_ms = 0;
    bool fail_wait = false;
    bool lost = false;
    bool fail_flush_registration = false;
    unsigned lose_after_flush_waits = 0;
    std::vector<void *> registered_events, unregistered_events;
    NVENCSTATUS submit_status = NV_ENC_SUCCESS;
    NVENCSTATUS flush_status = NV_ENC_SUCCESS;
    NVENCSTATUS lock_status = NV_ENC_SUCCESS;

  private:
    uint8_t event {}, flush_event {}, input {}, mapped {}, output {}, encoded_byte {42};
    uint64_t frame_index {};
    bool retiring = false;

    template<class... Args>
    static NVENCSTATUS NVENCAPI success(Args...) {
      return NV_ENC_SUCCESS;
    }

    bool init_library() override {
      nvenc = std::make_shared<NV_ENCODE_API_FUNCTION_LIST>();
      nvenc->nvEncOpenEncodeSessionEx = [](NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS *params, void **handle) {
        *handle = params->device;
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncGetEncodeGUIDCount = [](void *, uint32_t *count) {
        *count = 1;
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncGetEncodeGUIDs = [](void *, GUID *ids, uint32_t, uint32_t *count) {
        *ids = NV_ENC_CODEC_HEVC_GUID;
        *count = 1;
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncGetEncodeCaps = [](void *, GUID, NV_ENC_CAPS_PARAM *caps, int *value) {
        *value = (caps->capsToQuery == NV_ENC_CAPS_WIDTH_MAX || caps->capsToQuery == NV_ENC_CAPS_HEIGHT_MAX) ? 4096 : 1;
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncGetEncodePresetConfigEx = success;
      nvenc->nvEncInitializeEncoder = success;
      nvenc->nvEncRegisterAsyncEvent = [](void *handle, NV_ENC_EVENT_PARAMS *params) {
        auto &self = *static_cast<FakeEncoder *>(handle);
        if (self.fail_flush_registration && params->completionEvent == &self.flush_event) {
          return NV_ENC_ERR_INVALID_EVENT;
        }
        self.registered_events.emplace_back(params->completionEvent);
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncCreateBitstreamBuffer = [](void *handle, NV_ENC_CREATE_BITSTREAM_BUFFER *buffer) {
        buffer->bitstreamBuffer = &static_cast<FakeEncoder *>(handle)->output;
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncMapInputResource = [](void *handle, NV_ENC_MAP_INPUT_RESOURCE *buffer) {
        auto &self = *static_cast<FakeEncoder *>(handle);
        EXPECT_EQ(buffer->registeredResource, &self.input);
        self.calls.emplace_back("map");
        buffer->mappedResource = &self.mapped;
        buffer->mappedBufferFmt = NV_ENC_BUFFER_FORMAT_NV12;
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncEncodePicture = [](void *handle, NV_ENC_PIC_PARAMS *picture) {
        auto &self = *static_cast<FakeEncoder *>(handle);
        if (picture->encodePicFlags == NV_ENC_PIC_FLAG_EOS) {
          self.calls.emplace_back("flush");
          EXPECT_EQ(picture->inputBuffer, nullptr);
          EXPECT_EQ(picture->completionEvent, &self.flush_event);
          self.retiring = true;
          return self.flush_status;
        }
        EXPECT_EQ(picture->inputBuffer, &self.mapped);
        EXPECT_EQ(picture->outputBitstream, &self.output);
        self.calls.emplace_back("submit");
        self.retiring = false;
        EXPECT_EQ(picture->completionEvent, &self.event);
        self.frame_index = picture->inputTimeStamp;
        return self.submit_status;
      };
      nvenc->nvEncLockBitstream = [](void *handle, NV_ENC_LOCK_BITSTREAM *lock) {
        auto &self = *static_cast<FakeEncoder *>(handle);
        EXPECT_EQ(lock->outputBitstream, &self.output);
        EXPECT_EQ(self.ready_after_ms, 0u);
        EXPECT_TRUE(lock->doNotWait);
        self.calls.emplace_back(self.retiring ? "drain" : "read");
        if (self.lock_status != NV_ENC_SUCCESS) {
          return self.lock_status;
        }
        lock->bitstreamBufferPtr = &self.encoded_byte;
        lock->bitstreamSizeInBytes = 1;
        lock->outputTimeStamp = self.frame_index;
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncUnlockBitstream = [](void *handle, NV_ENC_OUTPUT_PTR buffer) {
        auto &self = *static_cast<FakeEncoder *>(handle);
        EXPECT_EQ(buffer, &self.output);
        self.calls.emplace_back("unlock");
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncUnmapInputResource = [](void *handle, NV_ENC_INPUT_PTR buffer) {
        auto &self = *static_cast<FakeEncoder *>(handle);
        EXPECT_EQ(buffer, &self.mapped);
        self.calls.emplace_back("unmap");
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncDestroyBitstreamBuffer = [](void *handle, NV_ENC_OUTPUT_PTR buffer) {
        auto &self = *static_cast<FakeEncoder *>(handle);
        EXPECT_EQ(buffer, &self.output);
        self.calls.emplace_back("destroy bitstream");
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncUnregisterAsyncEvent = [](void *handle, NV_ENC_EVENT_PARAMS *params) {
        static_cast<FakeEncoder *>(handle)->unregistered_events.emplace_back(params->completionEvent);
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncUnregisterResource = [](void *handle, NV_ENC_REGISTERED_PTR resource) {
        auto &self = *static_cast<FakeEncoder *>(handle);
        EXPECT_EQ(resource, &self.input);
        self.calls.emplace_back("unregister input");
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncDestroyEncoder = [](void *handle) {
        static_cast<FakeEncoder *>(handle)->calls.emplace_back("destroy encoder");
        return NV_ENC_SUCCESS;
      };
      return true;
    }

    bool create_and_register_input_buffer() override {
      registered_input_buffer = &input;
      return true;
    }

    bool device_removed() override {
      return lost;
    }

    void pause_before_retry(std::chrono::milliseconds duration) override {
      EXPECT_EQ(duration, std::chrono::seconds(1));
      calls.emplace_back("pause");
    }

    nvenc::async_wait_result wait_for_async_event(uint32_t timeout_ms, void *handle) override {
      EXPECT_TRUE(timeout_ms == 100u || timeout_ms == 1000u);
      const bool flush = handle == &flush_event;
      calls.emplace_back(flush ? "flush wait" : timeout_ms == 100u ? "wait" : "drain wait");
      if (flush && lose_after_flush_waits && --lose_after_flush_waits == 0) {
        lost = true;
      }
      if (fail_wait) {
        return nvenc::async_wait_result::failed;
      }
      auto &remaining = flush ? flush_ready_after_ms : ready_after_ms;
      if (remaining > timeout_ms) {
        remaining -= timeout_ms;
        return nvenc::async_wait_result::timeout;
      }
      remaining = 0;
      return nvenc::async_wait_result::signaled;
    }
  };

  TEST(NvencTimeoutTest, ReadyFrameUsesOnlyOneWait) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    const auto frame = encoder.encode_frame(17, false);
    EXPECT_EQ(frame.data, std::vector<uint8_t> {42});
    EXPECT_EQ(encoder.calls, (Calls {"map", "submit", "wait", "read", "unlock", "unmap"}));
  }

  TEST(NvencTimeoutTest, DelayedCompletionKeepsTheEncoderAndReturnsItsPacket) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.ready_after_ms = 150;
    const auto frame = encoder.encode_frame(17, false);
    EXPECT_EQ(frame.data, std::vector<uint8_t> {42});
    EXPECT_EQ(frame.frame_index, 17u);
    EXPECT_EQ(encoder.calls, (Calls {"map", "submit", "wait", "wait", "read", "unlock", "unmap"}));
    EXPECT_EQ(encoder.encode_frame(18, false).frame_index, 18u);
  }

  TEST(NvencTimeoutTest, PersistentTimeoutReturnsAfterSecondWait) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.ready_after_ms = 201;
    EXPECT_TRUE(encoder.encode_frame(17, false).data.empty());
    EXPECT_EQ(encoder.calls, (Calls {"map", "submit", "wait", "wait"}));
    EXPECT_TRUE(encoder.encode_frame(18, false).data.empty());
    encoder.calls.clear();
    encoder.destroy_encoder();
    EXPECT_EQ(encoder.calls, (Calls {"flush", "drain wait", "flush wait", "drain", "unlock", "unmap", "destroy bitstream", "unregister input", "destroy encoder"}));
    EXPECT_EQ(encoder.registered_events, encoder.unregistered_events);
    ASSERT_TRUE(encoder.open());
    EXPECT_EQ(encoder.encode_frame(19, false).frame_index, 19u);
  }

  TEST(NvencTimeoutTest, WaitFailureReturnsImmediately) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.fail_wait = true;
    EXPECT_TRUE(encoder.encode_frame(17, false).data.empty());
    EXPECT_EQ(encoder.calls, (Calls {"map", "submit", "wait"}));
    encoder.calls.clear();
    encoder.lost = true;
    encoder.destroy_encoder();
    EXPECT_EQ(encoder.calls, (Calls {"flush", "drain wait", "destroy encoder"}));
  }

  TEST(NvencTimeoutTest, SignaledFrameWithFailedLockDrainsWithoutWaitingForEventTwice) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.lock_status = NV_ENC_ERR_LOCK_BUSY;
    EXPECT_TRUE(encoder.encode_frame(17, false).data.empty());
    EXPECT_EQ(encoder.calls, (Calls {"map", "submit", "wait", "read"}));
    encoder.calls.clear();
    encoder.lock_status = NV_ENC_SUCCESS;
    encoder.destroy_encoder();
    EXPECT_EQ(encoder.calls, (Calls {"flush", "flush wait", "drain", "unlock", "unmap", "destroy bitstream", "unregister input", "destroy encoder"}));
  }

  TEST(NvencTimeoutTest, LostDeviceDuringDrainDoesNotUnmapInputOrDestroyBitstream) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.ready_after_ms = 250;
    EXPECT_TRUE(encoder.encode_frame(17, false).data.empty());
    encoder.calls.clear();
    encoder.lock_status = NV_ENC_ERR_DEVICE_NOT_EXIST;
    encoder.destroy_encoder();
    EXPECT_EQ(encoder.calls, (Calls {"flush", "drain wait", "flush wait", "drain", "destroy encoder"}));
  }

  TEST(NvencTimeoutTest, BufferedSubmissionStaysMappedUntilFlushed) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.submit_status = NV_ENC_ERR_NEED_MORE_INPUT;
    EXPECT_TRUE(encoder.encode_frame(17, false).data.empty());
    EXPECT_EQ(encoder.calls, (Calls {"map", "submit"}));
    encoder.calls.clear();
    encoder.destroy_encoder();
    EXPECT_EQ(encoder.calls, (Calls {"flush", "drain wait", "flush wait", "drain", "unlock", "unmap", "destroy bitstream", "unregister input", "destroy encoder"}));
  }

  TEST(NvencTimeoutTest, LateFlushHasAnIndependentCompletionEvent) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.ready_after_ms = 300;
    encoder.flush_ready_after_ms = 2300;
    EXPECT_TRUE(encoder.encode_frame(17, false).data.empty());
    encoder.calls.clear();
    encoder.destroy_encoder();
    EXPECT_EQ(encoder.calls, (Calls {"flush", "drain wait", "flush wait", "flush wait", "flush wait", "drain", "unlock", "unmap", "destroy bitstream", "unregister input", "destroy encoder"}));
  }

  TEST(NvencTimeoutTest, UnsignaledFlushRetainsBuffersUntilDeviceRemoval) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.ready_after_ms = 300;
    encoder.flush_ready_after_ms = UINT32_MAX;
    encoder.lose_after_flush_waits = 3;
    EXPECT_TRUE(encoder.encode_frame(17, false).data.empty());
    encoder.calls.clear();
    encoder.destroy_encoder();
    EXPECT_EQ(encoder.calls, (Calls {"flush", "drain wait", "flush wait", "flush wait", "flush wait", "destroy encoder"}));
  }

  TEST(NvencTimeoutTest, UnsignaledFlushReleasesTheSessionAtTheTimeLimit) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.retirement_timeout = std::chrono::seconds(3);
    encoder.ready_after_ms = 300;
    encoder.flush_ready_after_ms = UINT32_MAX;
    EXPECT_TRUE(encoder.encode_frame(17, false).data.empty());
    encoder.calls.clear();
    encoder.destroy_encoder();
    EXPECT_EQ(encoder.calls, (Calls {"flush", "drain wait", "flush wait", "flush wait", "flush wait", "unmap", "destroy bitstream", "unregister input", "destroy encoder"}));
    EXPECT_EQ(encoder.registered_events, encoder.unregistered_events);
    ASSERT_TRUE(encoder.open());
    EXPECT_EQ(encoder.encode_frame(18, false).frame_index, 18u);
  }

  TEST(NvencTimeoutTest, RejectedFlushIsRetriedUntilTheTimeLimit) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.retirement_timeout = std::chrono::seconds(2);
    encoder.ready_after_ms = 300;
    encoder.flush_status = NV_ENC_ERR_GENERIC;
    EXPECT_TRUE(encoder.encode_frame(17, false).data.empty());
    encoder.calls.clear();
    encoder.destroy_encoder();
    EXPECT_EQ(encoder.calls, (Calls {"flush", "pause", "flush", "pause", "flush", "unmap", "destroy bitstream", "unregister input", "destroy encoder"}));
    EXPECT_EQ(encoder.registered_events, encoder.unregistered_events);
  }

  TEST(NvencTimeoutTest, FailedFlushRegistrationReleasesOnlyTheRegisteredEvent) {
    FakeEncoder encoder;
    encoder.fail_flush_registration = true;
    EXPECT_FALSE(encoder.open());
    ASSERT_EQ(encoder.registered_events.size(), 1u);
    EXPECT_EQ(encoder.registered_events, encoder.unregistered_events);
  }

  TEST(NvencTimeoutTest, RejectedSubmissionReleasesUnsubmittedInput) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.submit_status = NV_ENC_ERR_INVALID_PARAM;
    EXPECT_TRUE(encoder.encode_frame(17, false).data.empty());
    EXPECT_EQ(encoder.calls, (Calls {"map", "submit", "unmap"}));
  }
}  // namespace

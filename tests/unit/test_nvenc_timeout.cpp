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

    Calls calls;
    uint32_t ready_after_ms = 0;
    bool fail_wait = false;

  private:
    uint8_t event {}, input {}, mapped {}, output {}, encoded_byte {42};
    uint64_t frame_index {};

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
      nvenc->nvEncRegisterAsyncEvent = success;
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
        EXPECT_EQ(picture->inputBuffer, &self.mapped);
        EXPECT_EQ(picture->outputBitstream, &self.output);
        self.calls.emplace_back("submit");
        self.frame_index = picture->inputTimeStamp;
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncLockBitstream = [](void *handle, NV_ENC_LOCK_BITSTREAM *lock) {
        auto &self = *static_cast<FakeEncoder *>(handle);
        EXPECT_EQ(lock->outputBitstream, &self.output);
        EXPECT_EQ(self.ready_after_ms, 0u);
        EXPECT_TRUE(lock->doNotWait);
        self.calls.emplace_back("read");
        lock->bitstreamBufferPtr = &self.encoded_byte;
        lock->bitstreamSizeInBytes = 1;
        lock->outputTimeStamp = self.frame_index;
        return NV_ENC_SUCCESS;
      };
      nvenc->nvEncUnlockBitstream = success;
      nvenc->nvEncUnmapInputResource = success;
      nvenc->nvEncDestroyBitstreamBuffer = success;
      nvenc->nvEncUnregisterAsyncEvent = success;
      nvenc->nvEncUnregisterResource = success;
      nvenc->nvEncDestroyEncoder = success;
      return true;
    }

    bool create_and_register_input_buffer() override {
      registered_input_buffer = &input;
      return true;
    }

    nvenc::async_wait_result wait_for_async_event(uint32_t timeout_ms) override {
      EXPECT_EQ(timeout_ms, 100u);
      calls.emplace_back("wait");
      if (fail_wait) {
        return nvenc::async_wait_result::failed;
      }
      if (ready_after_ms > timeout_ms) {
        ready_after_ms -= timeout_ms;
        return nvenc::async_wait_result::timeout;
      }
      ready_after_ms = 0;
      return nvenc::async_wait_result::signaled;
    }
  };

  TEST(NvencTimeoutTest, ReadyFrameUsesOnlyOneWait) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    const auto frame = encoder.encode_frame(17, false);
    EXPECT_EQ(frame.data, std::vector<uint8_t> {42});
    EXPECT_EQ(encoder.calls, (Calls {"map", "submit", "wait", "read"}));
  }

  TEST(NvencTimeoutTest, DelayedCompletionKeepsTheEncoderAndReturnsItsPacket) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.ready_after_ms = 150;
    const auto frame = encoder.encode_frame(17, false);
    EXPECT_EQ(frame.data, std::vector<uint8_t> {42});
    EXPECT_EQ(frame.frame_index, 17u);
    EXPECT_EQ(encoder.calls, (Calls {"map", "submit", "wait", "wait", "read"}));
    EXPECT_EQ(encoder.encode_frame(18, false).frame_index, 18u);
  }

  TEST(NvencTimeoutTest, PersistentTimeoutReturnsAfterSecondWait) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.ready_after_ms = 201;
    EXPECT_TRUE(encoder.encode_frame(17, false).data.empty());
    EXPECT_EQ(encoder.calls, (Calls {"map", "submit", "wait", "wait"}));
  }

  TEST(NvencTimeoutTest, WaitFailureReturnsImmediately) {
    FakeEncoder encoder;
    ASSERT_TRUE(encoder.open());
    encoder.fail_wait = true;
    EXPECT_TRUE(encoder.encode_frame(17, false).data.empty());
    EXPECT_EQ(encoder.calls, (Calls {"map", "submit", "wait"}));
  }
}  // namespace

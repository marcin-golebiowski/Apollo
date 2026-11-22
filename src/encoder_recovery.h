#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <optional>

namespace video {
  struct encode_run_result_t {
    bool failed {};
    bool produced_packet {};
  };

  // Each stream gets a fresh retry budget once it produces video again.
  class encoder_recovery_t {
  public:
    std::optional<std::chrono::milliseconds> retry_delay(encode_run_result_t result) {
      using namespace std::chrono_literals;
      static constexpr std::array delays {100ms, 250ms, 500ms};

      if (result.produced_packet) {
        retries = 0;
      }
      if (!result.failed) {
        return 0ms;
      }
      if (retries == delays.size()) {
        return std::nullopt;
      }
      return delays[retries++];
    }

  private:
    std::size_t retries {};
  };
}  // namespace video

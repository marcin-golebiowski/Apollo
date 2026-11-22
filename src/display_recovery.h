#pragma once

#include <algorithm>
#include <chrono>
#include <optional>
#include <thread>

namespace video {
  class display_recovery_t {
  public:
    using clock = std::chrono::steady_clock;
    static constexpr auto timeout = std::chrono::seconds(10);
    static constexpr auto retry_interval = std::chrono::milliseconds(200);

    clock::time_point begin() {
      const auto now = clock::now();
      if (!deadline) {
        deadline = now + timeout;
      } else {
        // The replacement reopened but asked to reset before capture resumed.
        std::this_thread::sleep_until(std::min(*deadline, now + retry_interval));
      }
      return *deadline;
    }

    void capture_resumed() {
      deadline.reset();
    }

  private:
    std::optional<clock::time_point> deadline;
  };
}  // namespace video

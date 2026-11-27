#pragma once

#include <chrono>
#include <cstddef>
#include <mutex>
#include <set>

namespace video {
  // Encoder sessions still being destroyed on detached threads, so one that
  // stays blocked in the driver shows up in the log instead of disappearing.
  class encoder_teardowns_t {
  public:
    using clock = std::chrono::steady_clock;

    struct status_t {
      std::size_t pending {};
      clock::duration oldest {};
    };

    // Returns the number of pending teardowns, including this one.
    std::size_t begin(clock::time_point started) {
      std::lock_guard lg {lock};
      starts.insert(started);
      return starts.size();
    }

    // Returns the number of teardowns still pending.
    std::size_t end(clock::time_point started) {
      std::lock_guard lg {lock};
      if (auto it = starts.find(started); it != starts.end()) {
        starts.erase(it);
      }
      return starts.size();
    }

    status_t status(clock::time_point now) const {
      std::lock_guard lg {lock};
      if (starts.empty()) {
        return {};
      }
      return {starts.size(), now - *starts.begin()};
    }

  private:
    mutable std::mutex lock;
    std::multiset<clock::time_point> starts;
  };
}  // namespace video

#ifndef __SRARQ_MOCK_H__
#define __SRARQ_MOCK_H__

#include <srarq.h>

#include <memory>
#include <unordered_map>
#include <vector>

namespace srarq {
namespace mock {
class MockTimer : public TimerInterface<uint8_t, uint32_t> {
 public:
  using TimerID = uint8_t;

  struct TimerInfo {
    uint32_t elapsed_time;
    uint32_t timeout;
    std::function<void()> callback;
  };

  MockTimer() = default;

  TimerID startTimer(uint32_t duration,
                     std::function<void()> callback) override {
    for (TimerID i = 0; i < 256; i++) {
      if (timers_.find(i) == timers_.end()) {
        timers_[i] = {0, duration, callback};
        return i;
      }
    }
    return 0;
  }

  void stopTimer(TimerID timer_id) override { timers_.erase(timer_id); }

  uint32_t getTimeout(TimerID timer_id) override {
    return timers_[timer_id].timeout;
  }

  uint32_t setTimeout(TimerID timer_id, uint32_t duration) override {
    timers_[timer_id].timeout = duration;
    return duration;
  }

  uint32_t getElapsedTime(TimerID timer_id) override {
    return timers_[timer_id].elapsed_time;
  }

  void tick(uint32_t elapsed_time) {
    for (auto& timer : timers_) {
      timer.second.elapsed_time += elapsed_time;
      if (timer.second.elapsed_time >= timer.second.timeout) {
        timer.second.callback();
      }
    }

    // delete expired timers
    for (auto it = timers_.begin(); it != timers_.end();) {
      if (it->second.elapsed_time >= it->second.timeout) {
        it = timers_.erase(it);
      } else {
        ++it;
      }
    }
  }

 private:
  std::unordered_map<TimerID, TimerInfo> timers_;
};
}  // namespace mock
}  // namespace srarq

#endif

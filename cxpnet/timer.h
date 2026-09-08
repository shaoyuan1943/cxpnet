#ifndef TIMER_H
#define TIMER_H

#include "sock.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace cxpnet {

  class Timer;
  class TimerManager;

  class Timer : public NonCopyable {
  public:
    using TimerID  = uint64_t;
    using Callback = std::function<void()>;

    Timer(TimerID id, uint32_t delay_ms, Callback cb);
    ~Timer() = default;

    TimerID  id() const { return id_; }
    uint32_t delay_ms() const { return delay_ms_; }
    bool     cancelled() const { return ACQUIRE_LOAD(cancelled_); }
    void     cancel() { RELEASE_STORE(cancelled_, true); }

    bool operator<(const Timer& other) const { return expire_time_ < other.expire_time_; }
  private:
    TimerID                               id_;
    uint32_t                              delay_ms_;
    Callback                              callback_;
    std::chrono::steady_clock::time_point expire_time_;
    std::atomic_bool                      cancelled_ {false};

    friend class TimerManager;
  };

  class TimerManager : public NonCopyable {
  public:
    explicit TimerManager(Closure wakeup_func = nullptr);
    ~TimerManager();

    Timer::TimerID add_timer(uint32_t delay_ms, Timer::Callback cb);
    void           cancel_timer(Timer::TimerID id);
    void           shutdown();
    uint32_t       next_timeout_ms(uint32_t default_timeout_ms);
    void           run_expired();
  private:
    std::vector<Timer::Callback> take_expired_callbacks_();
    struct TimerEntry {
      Timer::TimerID  id;
      Timer::Callback callback;
    };
    using TimePoint   = std::chrono::steady_clock::time_point;
    using ScheduleMap = std::multimap<TimePoint, TimerEntry>;
    using TimerIndex  = std::unordered_map<Timer::TimerID, ScheduleMap::iterator>;

    ScheduleMap      schedule_;
    TimerIndex       timer_index_;
    std::mutex       mutex_;
    std::atomic_bool running_ {true};
    Timer::TimerID   next_id_ {1};
    Closure          wakeup_func_ {nullptr};
  };

} // namespace cxpnet

#endif // TIMER_H

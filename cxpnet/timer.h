#ifndef TIMER_H
#define TIMER_H

#include "sock.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace cxpnet {

  class Timer;
  class TimerManager;

  // 定时器对象
  class Timer : public NonCopyable {
  public:
    using TimerID  = uint64_t;
    using Callback = std::function<void()>;

    Timer(TimerID id, uint32_t delay_ms, Callback cb);
    ~Timer() = default;

    TimerID  id() const { return id_; }
    uint32_t delay_ms() const { return delay_ms_; }
    bool     cancelled() const { return cancelled_.load(std::memory_order_acquire); }
    void     cancel() { cancelled_.store(true, std::memory_order_release); }
    void     execute() const;

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
    TimerManager();
    ~TimerManager();

    Timer::TimerID add_timer(uint32_t delay_ms, Timer::Callback cb);
    void           cancel_timer(Timer::TimerID id);
    void           shutdown();
  private:
    void timer_thread_func_();

    using TimePoint   = std::chrono::steady_clock::time_point;
    using ScheduleMap = std::multimap<TimePoint, Timer::TimerID>;

    std::unordered_map<Timer::TimerID, std::unique_ptr<Timer>> timers_;
    std::unordered_map<Timer::TimerID, ScheduleMap::iterator>  scheduled_timers_;
    ScheduleMap                                                schedule_;
    std::mutex                                                 mutex_;
    std::condition_variable                                    cv_;
    std::thread                                                timer_thread_;
    std::atomic_bool                                           running_ {true};
    Timer::TimerID                                             next_id_ {1};
  };

} // namespace cxpnet

#endif // TIMER_H

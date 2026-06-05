#include "timer.h"

namespace cxpnet {

  Timer::Timer(TimerID id, uint32_t delay_ms, Callback cb)
      : id_(id)
      , delay_ms_(delay_ms)
      , callback_(std::move(cb)) {
    expire_time_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
  }

  void Timer::execute() const {
    if (callback_ && !ACQUIRE_LOAD(cancelled_)) {
      callback_();
    }
  }

  TimerManager::TimerManager(Closure wakeup_func)
      : wakeup_func_(std::move(wakeup_func)) {}

  TimerManager::~TimerManager() { shutdown(); }

  Timer::TimerID TimerManager::add_timer(uint32_t delay_ms, Timer::Callback cb) {
    bool           should_wakeup = false;
    Timer::TimerID id            = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);

      if (!ACQUIRE_LOAD(running_)) { return 0; }

      id                    = next_id_++;
      auto timer            = std::make_unique<Timer>(id, delay_ms, std::move(cb));
      auto when             = timer->expire_time_;
      should_wakeup         = schedule_.empty() || when < schedule_.begin()->first;
      auto it               = schedule_.emplace(when, id);
      scheduled_timers_[id] = it;
      timers_[id]           = std::move(timer);
    }

    if (should_wakeup && wakeup_func_) { wakeup_func_(); }
    return id;
  }

  void TimerManager::cancel_timer(Timer::TimerID id) {
    bool should_wakeup = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);

      if (!ACQUIRE_LOAD(running_)) { return; }

      auto timer_it = timers_.find(id);
      if (timer_it == timers_.end()) { return; }

      timer_it->second->cancel();

      auto scheduled_it = scheduled_timers_.find(id);
      if (scheduled_it != scheduled_timers_.end()) {
        should_wakeup = scheduled_it->second == schedule_.begin();
        schedule_.erase(scheduled_it->second);
        scheduled_timers_.erase(scheduled_it);
      }

      timers_.erase(timer_it);
    }

    if (should_wakeup && wakeup_func_) { wakeup_func_(); }
  }

  void TimerManager::shutdown() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) { return; }

    std::lock_guard<std::mutex> lock(mutex_);
    timers_.clear();
    scheduled_timers_.clear();
    schedule_.clear();
  }

  uint32_t TimerManager::next_timeout_ms(uint32_t default_timeout_ms) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!ACQUIRE_LOAD(running_)) { return 0; }
    if (schedule_.empty()) { return default_timeout_ms; }

    auto now  = std::chrono::steady_clock::now();
    auto when = schedule_.begin()->first;
    if (when <= now) { return 0; }

    auto remaining = std::chrono::ceil<std::chrono::milliseconds>(when - now).count();
    if (remaining <= 0) { return 0; }
    if (static_cast<uint64_t>(remaining) < default_timeout_ms) {
      return static_cast<uint32_t>(remaining);
    }

    return default_timeout_ms;
  }

  void TimerManager::run_expired() {
    auto expired_callbacks = take_expired_callbacks_();
    for (auto& callback : expired_callbacks) {
      if (!ACQUIRE_LOAD(running_)) { break; }
      if (callback) { callback(); }
    }
  }

  std::vector<Timer::Callback> TimerManager::take_expired_callbacks_() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Timer::Callback> expired_callbacks;
    if (!ACQUIRE_LOAD(running_)) { return expired_callbacks; }

    auto now = std::chrono::steady_clock::now();
    while (!schedule_.empty()) {
      auto scheduled_it = schedule_.begin();
      if (scheduled_it->first > now) { break; }

      Timer::TimerID id = scheduled_it->second;
      schedule_.erase(scheduled_it);
      scheduled_timers_.erase(id);

      auto timer_it = timers_.find(id);
      if (timer_it == timers_.end()) { continue; }
      if (timer_it->second->cancelled()) {
        timers_.erase(timer_it);
        continue;
      }

      expired_callbacks.push_back(std::move(timer_it->second->callback_));
      timers_.erase(timer_it);
    }

    return expired_callbacks;
  }

} // namespace cxpnet

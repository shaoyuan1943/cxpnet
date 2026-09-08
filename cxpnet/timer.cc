#include "timer.h"

namespace cxpnet {

  Timer::Timer(TimerID id, uint32_t delay_ms, Callback cb)
      : id_(id)
      , delay_ms_(delay_ms)
      , callback_(std::move(cb)) {
    expire_time_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
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

      id            = next_id_++;
      auto when     = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
      should_wakeup = schedule_.empty() || when < schedule_.begin()->first;
      auto it       = schedule_.emplace(when, TimerEntry {id, std::move(cb)});
      timer_index_.emplace(id, it);
    }

    if (should_wakeup && wakeup_func_) { wakeup_func_(); }
    return id;
  }

  void TimerManager::cancel_timer(Timer::TimerID id) {
    bool should_wakeup = false;
    {
      std::lock_guard<std::mutex> lock(mutex_);

      if (!ACQUIRE_LOAD(running_)) { return; }

      auto index_it = timer_index_.find(id);
      if (index_it == timer_index_.end()) { return; }

      auto scheduled_it = index_it->second;
      should_wakeup     = scheduled_it == schedule_.begin();
      timer_index_.erase(index_it);
      schedule_.erase(scheduled_it);
    }

    if (should_wakeup && wakeup_func_) { wakeup_func_(); }
  }

  void TimerManager::shutdown() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) { return; }

    std::lock_guard<std::mutex> lock(mutex_);
    timer_index_.clear();
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

      expired_callbacks.push_back(std::move(scheduled_it->second.callback));
      timer_index_.erase(scheduled_it->second.id);
      schedule_.erase(scheduled_it);
    }

    return expired_callbacks;
  }

} // namespace cxpnet

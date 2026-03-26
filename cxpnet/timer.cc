#include "timer.h"

namespace cxpnet {

  Timer::Timer(TimerID id, uint32_t delay_ms, Callback cb)
      : id_(id)
      , delay_ms_(delay_ms)
      , callback_(std::move(cb)) {
    expire_time_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(delay_ms);
  }

  void Timer::execute() const {
    if (callback_ && !cancelled_.load(std::memory_order_acquire)) {
      callback_();
    }
  }

  TimerManager::TimerManager() {
    timer_thread_ = std::thread(&TimerManager::timer_thread_func_, this);
  }

  TimerManager::~TimerManager() { shutdown(); }

  Timer::TimerID TimerManager::add_timer(uint32_t delay_ms, Timer::Callback cb) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_.load(std::memory_order_acquire)) { return 0; }

    Timer::TimerID id     = next_id_++;
    auto           timer  = std::make_unique<Timer>(id, delay_ms, std::move(cb));
    auto           when   = timer->expire_time_;
    auto           it     = schedule_.emplace(when, id);
    scheduled_timers_[id] = it;
    timers_[id]           = std::move(timer);

    cv_.notify_one();
    return id;
  }

  void TimerManager::cancel_timer(Timer::TimerID id) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!running_.load(std::memory_order_acquire)) { return; }

    auto timer_it = timers_.find(id);
    if (timer_it == timers_.end()) { return; }

    timer_it->second->cancel();

    auto scheduled_it = scheduled_timers_.find(id);
    if (scheduled_it != scheduled_timers_.end()) {
      schedule_.erase(scheduled_it->second);
      scheduled_timers_.erase(scheduled_it);
    }

    timers_.erase(timer_it);
    cv_.notify_one();
  }

  void TimerManager::shutdown() {
    if (!running_.exchange(false, std::memory_order_acq_rel)) { return; }

    cv_.notify_all();

    if (timer_thread_.joinable()) {
      timer_thread_.join();
    }

    std::lock_guard<std::mutex> lock(mutex_);
    timers_.clear();
    scheduled_timers_.clear();
    schedule_.clear();
  }

  void TimerManager::timer_thread_func_() {
    while (running_.load(std::memory_order_acquire)) {
      std::unique_lock<std::mutex> lock_guard(mutex_);

      cv_.wait(lock_guard, [this] {
        return !running_.load(std::memory_order_acquire) || !schedule_.empty();
      });

      if (!running_.load(std::memory_order_acquire)) { break; }

      TimePoint earliest_expire_time = schedule_.begin()->first;
      (void)cv_.wait_until(lock_guard, earliest_expire_time, [this, earliest_expire_time] {
        if (!running_.load(std::memory_order_acquire)) { return true; }
        if (schedule_.empty()) { return true; }

        return schedule_.begin()->first < earliest_expire_time;
      });

      if (!running_.load(std::memory_order_acquire)) { break; }

      auto                         now = std::chrono::steady_clock::now();
      std::vector<Timer::Callback> expired_callbacks;

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

      lock_guard.unlock();

      for (auto& callback : expired_callbacks) {
        if (!running_.load(std::memory_order_acquire)) { break; }
        if (callback) {
          callback();
        }
      }
    }
  }

} // namespace cxpnet

#ifndef IO_POLL_H
#define IO_POLL_H

#include "base_type_value.h"
#include "channel.h"
#include "conn.h"
#include "platform_api.h"
#include "poller_for_epoll.h"
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace cxpnet {
  class IOEventPoll : public NonCopyable {
  public:
    IOEventPoll() {
      thread_id_      = std::this_thread::get_id();
      poller_         = std::make_unique<Poller>(this);
      wakeup_handle_  = platform::create_event_fd();
      wakeup_channel_ = std::make_unique<Channel>(this, wakeup_handle_);
      wakeup_channel_->set_read_callback(std::bind(&IOEventPoll::_handle_wakeup, this));
      wakeup_channel_->add_read_event();

      shut_.store(false, std::memory_order_release);
    }

    ~IOEventPoll() {
      platform::close_handle(wakeup_handle_);
    }

    // non-blocking
    void poll() {
      if (shut_.load(std::memory_order_acquire)) { return; }
      _poll(0);
    }

    // blocking
    void run() {
      thread_id_ = std::this_thread::get_id();
      while (!shut_.load(std::memory_order_acquire)) {
        _poll(kPollTimeoutMS);
      }
    }

    void shutdown() {
      if (shut_.load(std::memory_order_acquire)) { return; }

      shut_.store(true, std::memory_order_release);
      _notify_wakeup();
    }
    void run_in_poll(Closure func) {
      if (is_in_poll_thread()) {
        func();
        return;
      }

      std::lock_guard<std::mutex> lock(mutex_);
      tasks_.push_back(std::move(func));
      _notify_wakeup();
    }
    bool is_in_poll_thread() { return thread_id_ == std::this_thread::get_id(); }
    void update_channel(Channel* channel) { poller_->update_channel(channel); }
    void remove_channel(Channel* channel) { poller_->remove_channel(channel); }
    void set_error_callback(OnEventPollErrorCallback err_func) { on_err_func_ = std::move(err_func); }
  private:
    void _notify_wakeup() { platform::write_to_fd(wakeup_handle_); }
    void _handle_wakeup() { platform::read_from_fd(wakeup_handle_); }
    void _poll(uint32_t poll_timeout) {
      int result = 0;
      int err    = 0;
      active_channels_.clear();
      result = poller_->poll(kPollTimeoutMS, active_channels_);
      if (result < 0) { err = platform::get_last_error(); }

      for (auto&& channel : active_channels_) { channel->handle_event(); }

      std::vector<Closure> tmp_tasks;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.swap(tmp_tasks);
      }

      for (auto&& func : tmp_tasks) { func(); }

      if (err != 0 && err != EINTR && on_err_func_ != nullptr) {
        on_err_func_(this, err);
      }

      err = 0;
    }
  private:
    std::unique_ptr<Poller>  poller_;
    int                      wakeup_handle_ = -1;
    std::unique_ptr<Channel> wakeup_channel_;
    std::vector<Closure>     tasks_;
    std::mutex               mutex_;
    std::thread::id          thread_id_;
    std::vector<Channel*>    active_channels_;
    std::atomic<bool>        shut_;
    OnEventPollErrorCallback on_err_func_ = nullptr;
  };
} // namespace cxpnet

#endif // IO_POLL_H
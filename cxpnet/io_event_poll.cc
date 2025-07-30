#include "io_event_poll.h"
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
  IOEventPoll::IOEventPoll() {
    thread_id_      = std::this_thread::get_id();
    poller_         = std::make_unique<Poller>(this);
    wakeup_handle_  = Platform::create_event_fd();
    wakeup_channel_ = std::make_unique<Channel>(this, wakeup_handle_);
    wakeup_channel_->set_read_callback(std::bind(&IOEventPoll::_handle_wakeup, this));
    wakeup_channel_->add_read_event();

    shut_.store(false, std::memory_order_release);
  }

  IOEventPoll::~IOEventPoll() {
    Platform::close_handle(wakeup_handle_);
  }

  void IOEventPoll::poll() {
    if (shut_.load(std::memory_order_acquire)) { return; }
    _poll(0);
  }

  void IOEventPoll::run() {
    thread_id_ = std::this_thread::get_id();
    while (!shut_.load(std::memory_order_acquire)) {
      _poll(kPollTimeoutMS);
    }
  }

  void IOEventPoll::shutdown() {
    if (shut_.load(std::memory_order_acquire)) { return; }

    shut_.store(true, std::memory_order_release);
    _notify_wakeup();
  }
  void IOEventPoll::run_in_poll(Closure func) {
    if (is_in_poll_thread()) {
      func();
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push_back(std::move(func));
    _notify_wakeup();
  }

  void IOEventPoll::update_channel(Channel* channel) { poller_->update_channel(channel); }
  void IOEventPoll::remove_channel(Channel* channel) { poller_->remove_channel(channel); }
  void IOEventPoll::_poll(uint32_t poll_timeout) {
    int result = 0;
    int err    = 0;
    active_channels_.clear();
    result = poller_->poll(poll_timeout, active_channels_);
    if (result < 0) { err = Platform::get_last_error(); }

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
} // namespace cxpnet

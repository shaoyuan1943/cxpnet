#include "io_event_poll.h"
#include "channel.h"
#include "conn.h"
#include "platform_api.h"
#include "poller_for_epoll.h"
#include "timer.h"

#if CXP_PLATFORM_MACOS
#include "poller_for_kqueue.h"
#endif

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace cxpnet {
  IOEventPoll::IOEventPoll() {
    RELEASE_STORE(thread_id_, std::this_thread::get_id());

#if CXP_PLATFORM_LINUX
    poller_ = std::make_unique<EpollPoller>(this);
#elif CXP_PLATFORM_MACOS
    poller_ = std::make_unique<KqueuePoller>(this);
#endif

    wakeup_handle_  = Platform::create_wakeup_fd();
    wakeup_read_fd_ = Platform::get_wakeup_read_fd(wakeup_handle_);
    timer_manager_  = std::make_unique<TimerManager>([this]() {
      notify_wakeup_();
    });

    wakeup_channel_ = std::make_unique<Channel>(this, wakeup_read_fd_);
    wakeup_channel_->set_read_callback(std::bind(&IOEventPoll::handle_wakeup_, this));
    wakeup_channel_->add_read_event();
  }

  IOEventPoll::~IOEventPoll() {
    shutdown();

    if (wakeup_channel_) {
      wakeup_channel_->unregister();
      wakeup_channel_.reset();
    }

    Platform::destroy_wakeup_fd(wakeup_handle_);
    wakeup_handle_  = invalid_socket;
    wakeup_read_fd_ = invalid_socket;
  }

  void IOEventPoll::poll() {
    if (ACQUIRE_LOAD(closed_)) { return; }

    RELEASE_STORE(thread_id_, std::this_thread::get_id());
    RELEASE_STORE(polling_, true);
    poll_(0);
    RELEASE_STORE(polling_, false);
  }

  void IOEventPoll::run() {
    RELEASE_STORE(thread_id_, std::this_thread::get_id());
    RELEASE_STORE(polling_, true);

    while (!ACQUIRE_LOAD(closed_)) {
      uint32_t poll_timeout = timer_manager_->next_timeout_ms(kMaxIdlePollTimeoutMS);
      poll_(poll_timeout);
    }

    while (run_pending_tasks_()) {
    }

    RELEASE_STORE(polling_, false);
  }

  void IOEventPoll::shutdown() {
    if (ACQUIRE_LOAD(closed_)) { return; }

    RELEASE_STORE(closed_, true);
    if (timer_manager_) { timer_manager_->shutdown(); }
    notify_wakeup_();
  }

  void IOEventPoll::run_in_poll(Closure func) {
    if (is_in_poll_thread()) {
      func();
      return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push_back(std::move(func));
    notify_wakeup_();
  }

  void IOEventPoll::run_later(Closure func) {
    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push_back(std::move(func));
    notify_wakeup_();
  }

  void IOEventPoll::update_channel(Channel* channel) { poller_->update_channel(channel); }
  void IOEventPoll::unregister_channel(Channel* channel) { poller_->unregister_channel(channel); }

  // 已有一个未消费的唤醒信号时跳过重复写，减少高频投递下的 eventfd 系统调用
  void IOEventPoll::notify_wakeup_() {
    if (!wakeup_pending_.exchange(true, std::memory_order_acq_rel)) {
      Platform::wakeup_write(wakeup_handle_);
    }
  }
  void IOEventPoll::handle_wakeup_() {
    Platform::wakeup_read(wakeup_read_fd_);
    RELEASE_STORE(wakeup_pending_, false);
  }

  bool IOEventPoll::run_pending_tasks_() {
    std::vector<Closure> tmp_tasks;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (tasks_.empty()) { return false; }
      tasks_.swap(tmp_tasks);
    }

    for (auto&& func : tmp_tasks) {
      func();
    }

    return true;
  }

  void IOEventPoll::run_expired_timers_() {
    if (timer_manager_) {
      timer_manager_->run_expired();
    }
  }

  void IOEventPoll::poll_(uint32_t poll_timeout) {
    int result = 0;
    int err    = 0;

    run_expired_timers_();

    active_channels_.clear();
    result = poller_->poll(poll_timeout, active_channels_);
    if (result < 0) {
      err = Platform::get_last_error();
    }

    for (auto&& channel : active_channels_) {
      channel->handle_event();
    }

    run_pending_tasks_();
    run_expired_timers_();

    if (err != 0 && err != EINTR && on_err_func_ != nullptr) {
      on_err_func_(this, err);
    }

    err = 0;
  }
} // namespace cxpnet

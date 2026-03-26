#include "poller_for_kqueue.h"
#include "channel.h"
#include "ensure.h"
#include "io_event_poll.h"
#include "platform_api.h"

namespace cxpnet {
  KqueuePoller::KqueuePoller(IOEventPoll* owner_poll)
      : PollerBase(owner_poll) {
    kqueue_fd_ = ::kqueue();
    events_.resize(kMaxPollEventCount);
  }

  KqueuePoller::~KqueuePoller() {
    if (kqueue_fd_ >= 0) {
      Platform::close_handle(kqueue_fd_);
    }
  }

  void KqueuePoller::shutdown() {
    while (!channels_.empty()) {
      auto it = channels_.begin();
      remove_channel(it->second);
    }
    registered_events_.clear();
  }

  int KqueuePoller::poll(int timeout, std::vector<Channel*>& active_channels) {
    ENSURE(owner_poll_->is_in_poll_thread(), "Unsafe cross-thread operations");

    struct timespec ts;
    struct timespec* tsp = nullptr;
    if (timeout >= 0) {
      ts.tv_sec  = timeout / 1000;
      ts.tv_nsec = (timeout % 1000) * 1000000;
      tsp        = &ts;
    }

    int n = kevent(kqueue_fd_, nullptr, 0, events_.data(), static_cast<int>(events_.size()), tsp);
    if (n > 0) {
      fill_active_channels(n, active_channels);
      if (static_cast<size_t>(n) == events_.size()) { events_.resize(events_.size() * 2); }
    }

    return n;
  }

  void KqueuePoller::update_channel(Channel* channel) {
    int fd           = channel->handle();
    int events       = channel->events();
    int old_events   = registered_events_[fd];
    bool is_new      = !has_channel(fd);

    // 读事件变化
    if ((events & cxpnet::events::kRead) && !(old_events & cxpnet::events::kRead)) {
      register_read(fd);
    } else if (!(events & cxpnet::events::kRead) && (old_events & cxpnet::events::kRead)) {
      unregister_read(fd);
    }

    // 写事件变化
    if ((events & cxpnet::events::kWrite) && !(old_events & cxpnet::events::kWrite)) {
      register_write(fd);
    } else if (!(events & cxpnet::events::kWrite) && (old_events & cxpnet::events::kWrite)) {
      unregister_write(fd);
    }

    // 更新 channels_ 和 registered_events_
    if (events == 0) {
      // 移除注册
      if (has_channel(fd)) {
        channels_.erase(fd);
        registered_events_.erase(fd);
        channel->set_registered(false);
      }
    } else {
      // 添加或更新
      if (is_new) {
        channels_[fd] = channel;
        channel->set_registered(true);
      }
      registered_events_[fd] = events;
    }
  }

  void KqueuePoller::remove_channel(Channel* channel) {
    int fd = channel->handle();
    if (!has_channel(fd)) return;

    // 先取消所有事件
    auto it = registered_events_.find(fd);
    if (it != registered_events_.end()) {
      int old_events = it->second;
      if (old_events & cxpnet::events::kRead) {
        unregister_read(fd);
      }
      if (old_events & cxpnet::events::kWrite) {
        unregister_write(fd);
      }
      registered_events_.erase(it);
    }

    channels_.erase(fd);
    channel->set_registered(false);
  }

  void KqueuePoller::register_read(int fd) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr) < 0) {
      ENSURE(false, "kqueue register_read failed for fd {}", fd);
    }
  }

  void KqueuePoller::register_write(int fd) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr) < 0) {
      ENSURE(false, "kqueue register_write failed for fd {}", fd);
    }
  }

  void KqueuePoller::unregister_read(int fd) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, nullptr);
    kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr);
  }

  void KqueuePoller::unregister_write(int fd) {
    struct kevent ev;
    EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, nullptr);
    kevent(kqueue_fd_, &ev, 1, nullptr, 0, nullptr);
  }

  void KqueuePoller::fill_active_channels(int event_n, std::vector<Channel*>& active_channels) {
    // 使用临时 map 合并同一 fd 的多个事件
    std::unordered_map<int, int> fd_events;

    for (int i = 0; i < event_n; ++i) {
      int fd = static_cast<int>(events_[i].ident);
      auto it = channels_.find(fd);
      if (it == channels_.end()) continue;

      // 合并事件
      int revents = from_kqueue_events(events_[i]);
      fd_events[fd] |= revents;
    }

    // 设置结果事件并添加到 active_channels
    for (auto& [fd, revents] : fd_events) {
      auto it = channels_.find(fd);
      if (it != channels_.end()) {
        it->second->set_result_events(revents);
        active_channels.push_back(it->second);
      }
    }
  }

  // kqueue 事件 → 统一事件
  int KqueuePoller::from_kqueue_events(const struct kevent& ev) {
    int result = 0;

    // 读/写事件通过 filter 判断
    if (ev.filter == EVFILT_READ) {
      result |= cxpnet::events::kRead;
    }
    if (ev.filter == EVFILT_WRITE) {
      result |= cxpnet::events::kWrite;
    }

    // 错误和关闭通过 flags 判断
    if (ev.flags & EV_ERROR) {
      result |= cxpnet::events::kError;
    }
    if (ev.flags & EV_EOF) {
      result |= cxpnet::events::kHup;
    }

    return result;
  }
} // namespace cxpnet
